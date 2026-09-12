#pragma once
#include <array>
#include <atomic>
#include <shared_mutex>
#include <exception>
#include <tuple>
#include <type_traits>
#include <ESPressio_RuntimeIdentity.hpp>
#include <ESPressio_Synchronization.hpp>
#include <ESPressio_TypeDirectory.hpp>
#include "ESPressio_StateDescriptor.hpp"
#include "ESPressio_StatePersistence.hpp"
#include "ESPressio_StateRemoteReplica.hpp"
#include "ESPressio_StateRuntimeConfiguration.hpp"
#include "ESPressio_StateSubscription.hpp"
#include "ESPressio_StateTransportBinding.hpp"
namespace ESPressio::State {
namespace Detail {
template<class TState,class TFirst,class... TRest>
struct ConfigurationFor {
    using type=std::conditional_t<std::is_same_v<TState,typename TFirst::StateType>,TFirst,typename ConfigurationFor<TState,TRest...>::type>;
};
template<class TState,class TOnly>
struct ConfigurationFor<TState,TOnly> { using type=std::conditional_t<std::is_same_v<TState,typename TOnly::StateType>,TOnly,void>; };
template<class TState,class... Configs>
using ConfigurationForT=typename ConfigurationFor<TState,Configs...>::type;
template<class TState,class... Configs>
inline constexpr std::size_t ConfigurationCount=(std::size_t{0}+...+(std::is_same_v<TState,typename Configs::StateType>?1:0));
template<class C> constexpr bool ValidateDeploymentConfiguration() noexcept {
    using T=typename C::StateType;
    if constexpr(!T::IsTransmissibleState)
        static_assert(C::RemoteOwnerCapacity==0 && C::SubscriberCapacity==0,
                      "Only TransmissibleState Types may reserve remote-owner/subscriber capacity");
    else static_assert(T::ValidateTier(),"Transmissible State configuration requires a valid P2/P3 Type contract");
    return true;
}

template<class C>
struct StateSelectorState final {
    StateSubscriptionSelectorMode Mode=StateSubscriptionSelectorMode::None;
    std::array<StateSubscriptionHandle,C::RemoteOwnerCapacity> Active{};
    std::size_t Count=0;
    std::size_t InFlight=0;

    bool ContainsOwner(const System::DeviceIdentifier& owner) const noexcept {
        for(const auto& handle:Active) if(handle && handle.OwnerDevice==owner) return true;
        return false;
    }
    bool Contains(const StateSubscriptionHandle& wanted) const noexcept {
        for(const auto& handle:Active)
            if(handle.TypeId==wanted.TypeId && handle.OwnerDevice==wanted.OwnerDevice && handle.Session==wanted.Session) return true;
        return false;
    }
    bool Add(const StateSubscriptionHandle& handle) noexcept {
        if(!handle || ContainsOwner(handle.OwnerDevice)) return false;
        for(auto& slot:Active) if(!slot){slot=handle;++Count;return true;}
        return false;
    }
    bool Remove(const StateSubscriptionHandle& wanted) noexcept {
        for(auto& slot:Active) {
            if(slot.TypeId==wanted.TypeId && slot.OwnerDevice==wanted.OwnerDevice && slot.Session==wanted.Session) {
                slot={};--Count;return true;
            }
        }
        return false;
    }
};

template<std::size_t Capacity>
struct StateOwnerDiscoveryBuffer final {
    std::array<System::DeviceIdentifier,Capacity> Owners{};
    std::size_t Count=0;
    static bool Offer(void* context,const System::DeviceIdentifier& owner) noexcept {
        auto& self=*static_cast<StateOwnerDiscoveryBuffer*>(context);
        if(!owner) return false;
        for(std::size_t i=0;i<self.Count;++i) if(self.Owners[i]==owner) return true;
        if(self.Count==Capacity) return false;
        self.Owners[self.Count++]=owner;
        return true;
    }
};
}

template<class... TConfigurations>
class Runtime final {
    static_assert(sizeof...(TConfigurations)>0,"State Runtime requires at least one configured Type");
    static_assert((Detail::ValidateDeploymentConfiguration<TConfigurations>() && ...));
    using RemoteTables=std::tuple<StateRemoteReplicaTable<typename TConfigurations::StateType,
                                                          TConfigurations::RemoteOwnerCapacity,
                                                          TConfigurations::SubscriberCapacity>...>;
    using SelectorStates=std::tuple<Detail::StateSelectorState<TConfigurations>...>;
    bool _initialized=false;
    std::atomic<bool> _running{false};
    std::atomic<bool> _closing{false};
    mutable System::Synchronization::ReadWriteLock _lifecycle;
    bool _configurationError=false;
    RemoteTables _remote{};
    SelectorStates _selectors{};
    mutable System::Synchronization::Mutex _tokenMutex;

    template<class TState>
    auto& Table() noexcept {
        static_assert(Detail::ConfigurationCount<TState,TConfigurations...> == 1,
                      "State Type must appear exactly once in Runtime configuration");
        using C=Detail::ConfigurationForT<TState,TConfigurations...>;
        using TableType=StateRemoteReplicaTable<TState,C::RemoteOwnerCapacity,C::SubscriberCapacity>;
        return std::get<TableType>(_remote);
    }
    template<class TState>
    const auto& Table() const noexcept {
        static_assert(Detail::ConfigurationCount<TState,TConfigurations...> == 1,
                      "State Type must appear exactly once in Runtime configuration");
        using C=Detail::ConfigurationForT<TState,TConfigurations...>;
        using TableType=StateRemoteReplicaTable<TState,C::RemoteOwnerCapacity,C::SubscriberCapacity>;
        return std::get<TableType>(_remote);
    }
    template<class TState>
    auto& Selector() noexcept {
        using C=Detail::ConfigurationForT<TState,TConfigurations...>;
        static_assert(!std::is_void_v<C>,"State Type is not configured in this Runtime");
        return std::get<Detail::StateSelectorState<C>>(_selectors);
    }
    template<class TState>
    const auto& Selector() const noexcept {
        using C=Detail::ConfigurationForT<TState,TConfigurations...>;
        static_assert(!std::is_void_v<C>,"State Type is not configured in this Runtime");
        return std::get<Detail::StateSelectorState<C>>(_selectors);
    }
    template<class C>
    StateRuntimeStatus InitializeOne(Primitive::TypeDirectoryView directory,Timing::QualifiedTime(*capture)()) noexcept {
        using T=typename C::StateType;
        const auto* common=directory.Find({StateFamilyId,T::TypeId.Value()});
        if(!common) return StateRuntimeStatus::InvalidDirectory;
        const auto* descriptor=GetStateTypeDescriptor(*common);
        if(!descriptor || descriptor->TypeId!=T::TypeId) return StateRuntimeStatus::TypeConflict;
        auto& runtime=StateTypeRuntime<T>::Get();
        const auto initialized=runtime.Initialize(capture);
        if(initialized!=StateRuntimeStatus::Success) return initialized;
        if constexpr(Detail::IsRuntimeIdentityProjection<T>::value) {
            System::DeviceRuntimeIdentity identity{};
            if(!System::RuntimeIdentity::TryRead(identity)) {
                (void)runtime.RollbackInitialization(this);
                return StateRuntimeStatus::IdentityUnavailable;
            }
            const auto truthTime=capture ? capture() : Timing::SystemClock<>::GetInstance().CaptureQualifiedTime();
            const auto installed=runtime.InstallRuntimeIdentityProjection(
                typename T::ValueType{identity.Incarnation.Value()},truthTime);
            if(installed!=StateRuntimeStatus::Success) {
                (void)runtime.RollbackInitialization(this);
                return installed;
            }
        }
        return StateRuntimeStatus::Success;
    }
    template<class C>
    StateRuntimeStatus BindConvergenceOne() noexcept {
        Table<typename C::StateType>().InitializeSynchronization();
        if constexpr(C::SubscriberCapacity==0) return StateRuntimeStatus::Success;
        else return StateTypeRuntime<typename C::StateType>::Get().BindConvergence(Table<typename C::StateType>().ConvergenceView());
    }
    template<class C> void RollbackOne() noexcept { (void)StateTypeRuntime<typename C::StateType>::Get().RollbackInitialization(this); }
    template<class C> static bool ValidateOne() noexcept { return StateTypeRuntime<typename C::StateType>::Get().ValidateStart(); }

    static StateRemoteStatus MapTransport(StateTransportAdmissionStatus status) noexcept {
        switch(status) {
            case StateTransportAdmissionStatus::Accepted: return StateRemoteStatus::Success;
            case StateTransportAdmissionStatus::CapacityUnavailable: return StateRemoteStatus::CapacityUnavailable;
            case StateTransportAdmissionStatus::InvalidDestination: return StateRemoteStatus::InvalidIdentity;
            case StateTransportAdmissionStatus::Quiesced: return StateRemoteStatus::TransportUnavailable;
        }
        return StateRemoteStatus::TransportUnavailable;
    }
    static StateRemoteStatus MapDiscovery(StateOwnerDiscoveryStatus status) noexcept {
        switch(status) {
            case StateOwnerDiscoveryStatus::Success: return StateRemoteStatus::Success;
            case StateOwnerDiscoveryStatus::CapacityUnavailable: return StateRemoteStatus::CapacityUnavailable;
            case StateOwnerDiscoveryStatus::Unsupported: return StateRemoteStatus::TransportUnavailable;
            case StateOwnerDiscoveryStatus::Quiesced: return StateRemoteStatus::TransportUnavailable;
        }
        return StateRemoteStatus::TransportUnavailable;
    }
    static Primitive::PrimitiveAdmissionDisposition MapRemoteAdmission(StateRemoteStatus status) noexcept {
        using D=Primitive::PrimitiveAdmissionDisposition;
        switch(status) {
            case StateRemoteStatus::Success: return D::Accepted;
            case StateRemoteStatus::Duplicate:
            case StateRemoteStatus::Older: return D::AlreadyAccepted;
            case StateRemoteStatus::CapacityUnavailable: return D::ResourceUnavailable;
            case StateRemoteStatus::NotRunning:
            case StateRemoteStatus::Busy:
            case StateRemoteStatus::TransportUnavailable: return D::TemporarilyUnavailable;
            case StateRemoteStatus::NotFound:
            case StateRemoteStatus::InvalidIdentity:
            case StateRemoteStatus::InvalidSession:
            case StateRemoteStatus::SessionMismatch:
            case StateRemoteStatus::ProvenanceMismatch:
            case StateRemoteStatus::AwaitingResync:
            case StateRemoteStatus::ResyncRequired:
            case StateRemoteStatus::TokenExhausted:
            case StateRemoteStatus::Conflict: return D::Rejected;
        }
        return D::Rejected;
    }
    template<class TState>
    Primitive::PrimitiveAdmissionDisposition OfferReply(const StateOutboundMessage<TState>& reply) noexcept {
        const auto offered=StateTypeRuntime<TState>::Get().template AdmitOutbound<true>(reply);
        return offered ? Primitive::PrimitiveAdmissionDisposition::Accepted
                       : Primitive::PrimitiveAdmissionDisposition::TemporarilyUnavailable;
    }
    template<class TState>
    StateSubscriptionResult StartConcreteSubscription(const System::DeviceIdentifier& owner,StateSubscriptionSelectorMode mode) noexcept {
        using C=Detail::ConfigurationForT<TState,TConfigurations...>;
        static_assert(!std::is_void_v<C> && TState::IsTransmissibleState);
        static_assert(C::RemoteOwnerCapacity>0,"Remote State subscription requires MaximumRemoteOwners > 0");
        if(!IsRunning()) return {StateRemoteStatus::NotRunning,{}};
        if(!owner || !StateTypeRuntime<TState>::Get().HasTransport()) return {StateRemoteStatus::TransportUnavailable,{}};
        System::DeviceRuntimeIdentity requester{};
        if(!System::RuntimeIdentity::TryRead(requester)) return {StateRemoteStatus::InvalidIdentity,{}};

        StateSessionToken token{};
        bool resetSpecificOnFailure=false;
        {
            std::lock_guard<System::Synchronization::Mutex> lock(_tokenMutex);
            auto& selector=Selector<TState>();
            if(selector.ContainsOwner(owner)) return {StateRemoteStatus::Conflict,{}};
            if(mode==StateSubscriptionSelectorMode::SpecificDevice) {
                if(selector.Mode==StateSubscriptionSelectorMode::AnyDevice) return {StateRemoteStatus::Conflict,{}};
                if(selector.Mode==StateSubscriptionSelectorMode::None) {
                    selector.Mode=StateSubscriptionSelectorMode::SpecificDevice;
                    resetSpecificOnFailure=true;
                }
            } else {
                if(selector.Mode!=StateSubscriptionSelectorMode::AnyDevice) return {StateRemoteStatus::Conflict,{}};
            }
            ++selector.InFlight;
            if(!Detail::StateProcessTokenAuthority::TryAllocate(token)) {
                --selector.InFlight;
                if(resetSpecificOnFailure && selector.Count==0 && selector.InFlight==0) selector.Mode=StateSubscriptionSelectorMode::None;
                return {StateRemoteStatus::TokenExhausted,{}};
            }
        }

        StateSnapshot<TState> oldSnapshot{};
        const bool hadRetained=Table<TState>().TryReadRemote(owner,oldSnapshot);
        const auto reserved=Table<TState>().ReserveRemoteOwner(owner,token);
        if(reserved!=StateRemoteStatus::Success) {
            std::lock_guard<System::Synchronization::Mutex> lock(_tokenMutex);
            auto& selector=Selector<TState>();
            --selector.InFlight;
            if(resetSpecificOnFailure && selector.Count==0 && selector.InFlight==0) selector.Mode=StateSubscriptionSelectorMode::None;
            return {reserved,{}};
        }

        StateOutboundMessage<TState> request{};
        request.Kind=StateMessageKind::SubscribeRequest;
        request.Owner={owner,{}};
        request.Requester=requester;
        request.Session=token;
        const auto admitted=StateTypeRuntime<TState>::Get().AdmitOutbound(request);
        if(!admitted) {
            (void)Table<TState>().Unsubscribe(owner,hadRetained ? StateReplicaRelease::RetainLastKnown
                                                             : StateReplicaRelease::ReleaseReplica);
            std::lock_guard<System::Synchronization::Mutex> lock(_tokenMutex);
            auto& selector=Selector<TState>();
            --selector.InFlight;
            if(resetSpecificOnFailure && selector.Count==0 && selector.InFlight==0) selector.Mode=StateSubscriptionSelectorMode::None;
            return {MapTransport(admitted.Status),{}};
        }

        StateSubscriptionHandle handle{TState::TypeId,owner,token};
        {
            std::lock_guard<System::Synchronization::Mutex> lock(_tokenMutex);
            auto& selector=Selector<TState>();
            if(!selector.Add(handle)) std::terminate();
            --selector.InFlight;
        }
        return {StateRemoteStatus::Success,handle};
    }

public:
    Runtime()=default;
    ~Runtime() {
        if(_initialized) (void)Shutdown();
        else (StateTypeRuntime<typename TConfigurations::StateType>::Get().ReleaseStagedFamily(this),...);
    }
    Runtime(const Runtime&)=delete;
    Runtime& operator=(const Runtime&)=delete;

    template<class TState>
    StateOwner<TState> BindOwner() noexcept {
        using C=Detail::ConfigurationForT<TState,TConfigurations...>;
        static_assert(!std::is_void_v<C>,"State Type is not configured in this Runtime");
        static_assert(!Detail::IsRuntimeIdentityProjection<TState>::value,
                      "DeviceRuntimeIncarnationState is a read-only System projection and exposes no StateOwner");
        if(_initialized) return {};
        if(StateTypeRuntime<TState>::Get().ClaimFamily(this)!=StateRuntimeStatus::Success) {
            _configurationError=true;return {};
        }
        auto owner=StateTypeRuntime<TState>::Get().BindOwner();
        if(!owner) _configurationError=true;
        return owner;
    }
    template<class TState,class Format>
    StateRuntimeStatus BindPersistence(StatePersistenceBinding<TState,Format>& binding) noexcept {
        using C=Detail::ConfigurationForT<TState,TConfigurations...>;
        static_assert(!std::is_void_v<C>,"State Type is not configured in this Runtime");
        if(_initialized) return StateRuntimeStatus::Frozen;
        const auto claimed=StateTypeRuntime<TState>::Get().ClaimFamily(this);
        if(claimed!=StateRuntimeStatus::Success) { _configurationError=true;return claimed; }
        const auto status=StateTypeRuntime<TState>::Get().BindPersistence(binding.View());
        if(status!=StateRuntimeStatus::Success) _configurationError=true;
        return status;
    }
    template<class TState,class Format>
    StateRuntimeStatus BindTransport(StateTransportBinding<TState,Format>& binding) noexcept {
        using C=Detail::ConfigurationForT<TState,TConfigurations...>;
        static_assert(!std::is_void_v<C>,"State Type is not configured in this Runtime");
        static_assert(TState::IsTransmissibleState,"Only TransmissibleState Types may bind transport adapters");
        if(_initialized) return StateRuntimeStatus::Frozen;
        const auto claimed=StateTypeRuntime<TState>::Get().ClaimFamily(this);
        if(claimed!=StateRuntimeStatus::Success) { _configurationError=true;return claimed; }
        const auto status=StateTypeRuntime<TState>::Get().BindTransport(binding.View());
        if(status!=StateRuntimeStatus::Success) _configurationError=true;
        return status;
    }
    StateRuntimeStatus Initialize(Primitive::TypeDirectoryView directory,Timing::QualifiedTime(*capture)()=nullptr) noexcept {
        if(_initialized) return StateRuntimeStatus::AlreadyInitialized;
        if(_configurationError) return StateRuntimeStatus::InvalidConfiguration;
        if(!directory.IsFrozen()) return StateRuntimeStatus::InvalidDirectory;
        { std::unique_lock<System::Synchronization::ReadWriteLock> lock(_lifecycle); }
        { std::lock_guard<System::Synchronization::Mutex> lock(_tokenMutex); }
        Detail::StateProcessTokenAuthority::Initialize();
        StateRuntimeStatus status=StateRuntimeStatus::Success;
        bool ok=true;
        // Claims precede preparation, so rollback can never touch a Type staged
        // by another Runtime. Own configuration stays staged for a corrected retry.
        auto claim=[&](auto tag){using C=decltype(tag);if(ok){status=StateTypeRuntime<typename C::StateType>::Get().ClaimFamily(this);ok=status==StateRuntimeStatus::Success;}};
        (claim(TConfigurations{}),...);
        if(!ok) return status;
        auto bind=[&](auto tag){using C=decltype(tag);if(ok){status=BindConvergenceOne<C>();ok=status==StateRuntimeStatus::Success;}};
        (bind(TConfigurations{}),...);
        if(!ok) return status;
        auto initialize=[&](auto tag){using C=decltype(tag);if(ok){status=InitializeOne<C>(directory,capture);ok=status==StateRuntimeStatus::Success;}};
        (initialize(TConfigurations{}),...);
        if(!ok){(RollbackOne<TConfigurations>(),...);return status;}
        _initialized=true;
        return StateRuntimeStatus::Success;
    }
    StateRuntimeStatus Start() noexcept {
        if(_closing.load(std::memory_order_acquire)) return StateRuntimeStatus::Stopping;
        std::unique_lock<System::Synchronization::ReadWriteLock> activity(_lifecycle);
        if(_closing.load(std::memory_order_acquire)) return StateRuntimeStatus::Stopping;
        if(!_initialized) return StateRuntimeStatus::NotInitialized;
        if(_running) return StateRuntimeStatus::Frozen;
        if(!(ValidateOne<TConfigurations>() && ...)) return StateRuntimeStatus::InvalidConfiguration;
        (StateTypeRuntime<typename TConfigurations::StateType>::Get().StartValidated(),...);
        _running=true;
        return StateRuntimeStatus::Success;
    }
    StateRuntimeStatus Shutdown() noexcept {
        if(!_initialized) return StateRuntimeStatus::NotInitialized;
        // Close before waiting for active callers. No callback may initiate Shutdown
        // from inside a Runtime operation whose lease it would itself need to drain.
        _closing.store(true,std::memory_order_release);
        _running.store(false,std::memory_order_release);
        (StateTypeRuntime<typename TConfigurations::StateType>::Get().RequestStop(),...);
        std::unique_lock<System::Synchronization::ReadWriteLock> activity(_lifecycle);
        _running.store(false,std::memory_order_release);
        (StateTypeRuntime<typename TConfigurations::StateType>::Get().Shutdown(),...);
        (Table<typename TConfigurations::StateType>().CloseSessions(),...);
        { std::lock_guard<System::Synchronization::Mutex> lock(_tokenMutex);_selectors={}; }
        return StateRuntimeStatus::Success;
    }

    template<class TState> bool TryRead(StateSnapshot<TState>& output) const noexcept {
        using C=Detail::ConfigurationForT<TState,TConfigurations...>;
        static_assert(!std::is_void_v<C>,"State Type is not configured in this Runtime");
        return StateTypeRuntime<TState>::Get().TryRead(output);
    }
    template<class TState> StateVersion Version() const noexcept { return StateTypeRuntime<TState>::Get().Version(); }
    template<class TState> static constexpr std::size_t MaximumRemoteOwnersFor() noexcept {
        using C=Detail::ConfigurationForT<TState,TConfigurations...>;
        static_assert(!std::is_void_v<C>);
        return C::RemoteOwnerCapacity;
    }
    template<class TState> static constexpr std::size_t MaximumSubscribersFor() noexcept {
        using C=Detail::ConfigurationForT<TState,TConfigurations...>;
        static_assert(!std::is_void_v<C>);
        return C::SubscriberCapacity;
    }

    template<class TState>
    StateSubscriptionResult ReserveSubscriptionSession(const System::DeviceIdentifier& owner) noexcept {
        if(!IsRunning()) return {StateRemoteStatus::NotRunning,{}};
        std::shared_lock<System::Synchronization::ReadWriteLock> activity(_lifecycle);
        using C=Detail::ConfigurationForT<TState,TConfigurations...>;
        static_assert(!std::is_void_v<C> && TState::IsTransmissibleState);
        if(!IsRunning()) return {StateRemoteStatus::NotRunning,{}};
        StateSessionToken token{};
        {
            std::lock_guard<System::Synchronization::Mutex> lock(_tokenMutex);
            if(!Detail::StateProcessTokenAuthority::TryAllocate(token)) return {StateRemoteStatus::TokenExhausted,{}};
        }
        const auto status=Table<TState>().ReserveRemoteOwner(owner,token);
        if(status!=StateRemoteStatus::Success) return {status,{}};
        return {StateRemoteStatus::Success,{TState::TypeId,owner,token}};
    }

    /// <summary>Starts one concrete remote-owner session and emits a semantic SubscribeRequest.</summary>
    template<class TState>
    StateSubscriptionResult SubscribeFrom(const System::DeviceIdentifier& owner) noexcept {
        if(!IsRunning()) return {StateRemoteStatus::NotRunning,{}};
        std::shared_lock<System::Synchronization::ReadWriteLock> activity(_lifecycle);
        return StartConcreteSubscription<TState>(owner,StateSubscriptionSelectorMode::SpecificDevice);
    }

    /// <summary>Expands AnyDevice locally through the bound adapter, then starts concrete owner sessions.</summary>
    /// <remarks>No AnyDevice value is encoded on State V1 wire. Discovery must complete within the Type's
    /// MaximumRemoteOwners capacity before any subscription request is emitted.</remarks>
    template<class TState>
    auto SubscribeAny() noexcept {
        using C=Detail::ConfigurationForT<TState,TConfigurations...>;
        static_assert(!std::is_void_v<C> && TState::IsTransmissibleState);
        static_assert(C::RemoteOwnerCapacity>0,"SubscribeAny requires MaximumRemoteOwners > 0");
        StateAnySubscriptionResult<C::RemoteOwnerCapacity> result{};
        if(!IsRunning()) { result.Status=StateRemoteStatus::NotRunning; return result; }
        std::shared_lock<System::Synchronization::ReadWriteLock> activity(_lifecycle);
        if(!IsRunning()) { result.Status=StateRemoteStatus::NotRunning; return result; }
        if(!StateTypeRuntime<TState>::Get().HasTransport() || !StateTypeRuntime<TState>::Get().SupportsOwnerDiscovery()) {
            result.Status=StateRemoteStatus::TransportUnavailable;return result;
        }
        {
            std::lock_guard<System::Synchronization::Mutex> lock(_tokenMutex);
            auto& selector=Selector<TState>();
            if(selector.Mode!=StateSubscriptionSelectorMode::None) { result.Status=StateRemoteStatus::Conflict;return result; }
            selector.Mode=StateSubscriptionSelectorMode::AnyDevice;
        }
        Detail::StateOwnerDiscoveryBuffer<C::RemoteOwnerCapacity> owners{};
        const auto discovered=StateTypeRuntime<TState>::Get().DiscoverOwners({&owners,&Detail::StateOwnerDiscoveryBuffer<C::RemoteOwnerCapacity>::Offer});
        if(discovered!=StateOwnerDiscoveryStatus::Success) {
            std::lock_guard<System::Synchronization::Mutex> lock(_tokenMutex);
            auto& selector=Selector<TState>();
            if(selector.Count==0 && selector.InFlight==0) selector.Mode=StateSubscriptionSelectorMode::None;
            result.Status=MapDiscovery(discovered);return result;
        }
        for(std::size_t i=0;i<owners.Count;++i) {
            const auto started=StartConcreteSubscription<TState>(owners.Owners[i],StateSubscriptionSelectorMode::AnyDevice);
            if(!started) {
                if(result.SessionCount==0) {
                    std::lock_guard<System::Synchronization::Mutex> lock(_tokenMutex);
                    auto& selector=Selector<TState>();
                    if(selector.Count==0 && selector.InFlight==0) selector.Mode=StateSubscriptionSelectorMode::None;
                }
                result.Status=started.Status;return result;
            }
            result.Sessions[result.SessionCount++]=started.Handle;
        }
        result.Status=StateRemoteStatus::Success;
        return result;
    }

    /// <summary>Handles a later concrete-owner discovery for an already-active AnyDevice selector.</summary>
    template<class TState>
    StateSubscriptionResult ExpandAnyTo(const System::DeviceIdentifier& owner) noexcept {
        if(!IsRunning()) return {StateRemoteStatus::NotRunning,{}};
        std::shared_lock<System::Synchronization::ReadWriteLock> activity(_lifecycle);
        {
            std::lock_guard<System::Synchronization::Mutex> lock(_tokenMutex);
            if(Selector<TState>().Mode!=StateSubscriptionSelectorMode::AnyDevice) return {StateRemoteStatus::Conflict,{}};
        }
        return StartConcreteSubscription<TState>(owner,StateSubscriptionSelectorMode::AnyDevice);
    }

    /// <summary>Closes local admission before attempting the bounded remote Unsubscribe notification.</summary>
    /// <remarks>Success denotes local closure. Adapter backpressure cannot keep this session active;
    /// lost remote notification is reclaimed by the source's bounded continuity/replacement rules.</remarks>
    template<class TState>
    StateRemoteStatus Unsubscribe(const StateSubscriptionHandle& handle,StateReplicaRelease disposition) noexcept {
        if(!IsRunning()) return StateRemoteStatus::NotRunning;
        std::shared_lock<System::Synchronization::ReadWriteLock> activity(_lifecycle);
        if(!IsRunning()) return StateRemoteStatus::NotRunning;
        if(handle.TypeId!=TState::TypeId || !handle) return StateRemoteStatus::InvalidSession;
        System::DeviceRuntimeIdentity requester{};
        if(!System::RuntimeIdentity::TryRead(requester)) return StateRemoteStatus::InvalidIdentity;
        System::DeviceRuntimeIdentity closedOwner{};
        {
            // Hold selector ownership through replica closure: an old handle must never close a
            // replacement session admitted for the same owner while this operation is in flight.
            std::lock_guard<System::Synchronization::Mutex> lock(_tokenMutex);
            auto& selector=Selector<TState>();
            if(!selector.Contains(handle)) return StateRemoteStatus::SessionMismatch;
            const auto status=Table<TState>().Unsubscribe(handle.OwnerDevice,disposition,handle.Session,&closedOwner);
            if(status!=StateRemoteStatus::Success) return status;
            (void)selector.Remove(handle);
            if(selector.Count==0 && selector.InFlight==0) selector.Mode=StateSubscriptionSelectorMode::None;
        }
        // Before a baseline reply, the current owner incarnation is unknown. Closing locally is
        // still required, but no invalid zero-incarnation Unsubscribe is offered to the V1 codec.
        if(!closedOwner) return StateRemoteStatus::Success;
        StateOutboundMessage<TState> request{};
        request.Kind=StateMessageKind::UnsubscribeRequest;
        request.Owner=closedOwner;
        request.Requester=requester;
        request.Session=handle.Session;
        // The adapter borrows only the old immutable session identity, outside local locks.
        // Its admission result is not permission to restore local subscription authority.
        (void)StateTypeRuntime<TState>::Get().AdmitOutbound(request);
        return StateRemoteStatus::Success;
    }

    template<class TState>
    StateRemoteStatus StopAny() noexcept {
        if(!IsRunning()) return StateRemoteStatus::NotRunning;
        std::shared_lock<System::Synchronization::ReadWriteLock> activity(_lifecycle);
        if(!IsRunning()) return StateRemoteStatus::NotRunning;
        std::lock_guard<System::Synchronization::Mutex> lock(_tokenMutex);
        auto& selector=Selector<TState>();
        if(selector.Mode!=StateSubscriptionSelectorMode::AnyDevice) return StateRemoteStatus::Conflict;
        if(selector.Count!=0 || selector.InFlight!=0) return StateRemoteStatus::Conflict;
        selector.Mode=StateSubscriptionSelectorMode::None;
        return StateRemoteStatus::Success;
    }

    template<class TState> StateSubscriptionSelectorMode SubscriptionSelectorMode() const noexcept {
        std::lock_guard<System::Synchronization::Mutex> lock(_tokenMutex);
        return Selector<TState>().Mode;
    }

    StateRemoteStatus AllocateResyncToken(StateResyncToken& output) noexcept {
        if(!IsRunning()) return StateRemoteStatus::NotRunning;
        std::shared_lock<System::Synchronization::ReadWriteLock> activity(_lifecycle);
        if(!IsRunning()) return StateRemoteStatus::NotRunning;
        std::lock_guard<System::Synchronization::Mutex> lock(_tokenMutex);
        return Detail::StateProcessTokenAuthority::TryAllocate(output)?StateRemoteStatus::Success:StateRemoteStatus::TokenExhausted;
    }

    template<class TState> StateRemoteStatus InstallSubscribeSnapshot(const System::DeviceRuntimeIdentity& owner,StateSessionToken session,
                                                                       StateVersion version,const StateSnapshot<TState>& snapshot) noexcept {
        if(!IsRunning()) return StateRemoteStatus::NotRunning;
        std::shared_lock<System::Synchronization::ReadWriteLock> activity(_lifecycle);
        return IsRunning()?Table<TState>().InstallSubscribeSnapshot(owner,session,version,snapshot):StateRemoteStatus::NotRunning;
    }
    template<class TState> StateRemoteStatus InstallSubscribeNoValue(const System::DeviceRuntimeIdentity& owner,StateSessionToken session) noexcept {
        if(!IsRunning()) return StateRemoteStatus::NotRunning;
        std::shared_lock<System::Synchronization::ReadWriteLock> activity(_lifecycle);
        return IsRunning()?Table<TState>().InstallSubscribeNoValue(owner,session):StateRemoteStatus::NotRunning;
    }
    template<class TState> StateRemoteStatus InstallBaselineSnapshot(const System::DeviceRuntimeIdentity& owner,StateSessionToken session,
                                                                     StateVersion version,const StateSnapshot<TState>& snapshot) noexcept {
        if(!IsRunning()) return StateRemoteStatus::NotRunning;
        std::shared_lock<System::Synchronization::ReadWriteLock> activity(_lifecycle);
        return IsRunning()?Table<TState>().InstallBaselineSnapshot(owner,session,version,snapshot):StateRemoteStatus::NotRunning;
    }
    template<class TState> StateRemoteStatus ApplyRemotePublication(const System::DeviceRuntimeIdentity& owner,StateSessionToken session,
                                                                    StateVersion version,const StateSnapshot<TState>& snapshot) noexcept {
        if(!IsRunning()) return StateRemoteStatus::NotRunning;
        std::shared_lock<System::Synchronization::ReadWriteLock> activity(_lifecycle);
        return IsRunning()?Table<TState>().ApplyPublication(owner,session,version,snapshot):StateRemoteStatus::NotRunning;
    }
    template<class TState> StateRemoteStatus RequireRemoteResync(const System::DeviceIdentifier& owner) noexcept {
        if(!IsRunning()) return StateRemoteStatus::NotRunning;
        std::shared_lock<System::Synchronization::ReadWriteLock> activity(_lifecycle);
        return IsRunning()?Table<TState>().RequireResync(owner):StateRemoteStatus::NotRunning;
    }
    template<class TState> StateRemoteStatus BeginRemoteResync(const System::DeviceIdentifier& owner,StateResyncToken token) noexcept {
        if(!IsRunning()) return StateRemoteStatus::NotRunning;
        std::shared_lock<System::Synchronization::ReadWriteLock> activity(_lifecycle);
        return IsRunning()?Table<TState>().BeginResync(owner,token):StateRemoteStatus::NotRunning;
    }
    template<class TState> StateRemoteStatus InstallResyncSnapshot(const System::DeviceRuntimeIdentity& owner,StateSessionToken session,
                                                                   StateResyncToken token,StateVersion version,const StateSnapshot<TState>& snapshot) noexcept {
        if(!IsRunning()) return StateRemoteStatus::NotRunning;
        std::shared_lock<System::Synchronization::ReadWriteLock> activity(_lifecycle);
        return IsRunning()?Table<TState>().InstallResyncSnapshot(owner,session,token,version,snapshot):StateRemoteStatus::NotRunning;
    }
    template<class TState> bool TryReadRemote(const System::DeviceIdentifier& owner,StateSnapshot<TState>& output) const noexcept {
        return Table<TState>().TryReadRemote(owner,output);
    }
    template<class TState> StateRemoteSessionState GetRemoteSessionStatus(const System::DeviceIdentifier& owner) const noexcept {
        return Table<TState>().SessionState(owner);
    }
    template<class TState> StateRemoteStatus Unsubscribe(const System::DeviceIdentifier& owner,StateReplicaRelease disposition) noexcept {
        if(!IsRunning()) return StateRemoteStatus::NotRunning;
        std::shared_lock<System::Synchronization::ReadWriteLock> activity(_lifecycle);
        return IsRunning()?Table<TState>().Unsubscribe(owner,disposition):StateRemoteStatus::NotRunning;
    }
    template<class TState> StateRemoteStatus ForgetRemote(const System::DeviceIdentifier& owner) noexcept {
        if(!IsRunning()) return StateRemoteStatus::NotRunning;
        std::shared_lock<System::Synchronization::ReadWriteLock> activity(_lifecycle);
        return IsRunning()?Table<TState>().ForgetRemote(owner):StateRemoteStatus::NotRunning;
    }
    template<class TState> std::size_t RemoteOwnersInUse() const noexcept { return Table<TState>().RemoteOwnersInUse(); }

    template<class TState> StateRemoteStatus ReserveSourceSubscriber(const System::DeviceRuntimeIdentity& requester,StateSessionToken session) noexcept {
        if(!IsRunning()) return StateRemoteStatus::NotRunning;
        std::shared_lock<System::Synchronization::ReadWriteLock> activity(_lifecycle);
        return IsRunning()?Table<TState>().ReserveSubscriber(requester,session):StateRemoteStatus::NotRunning;
    }
    template<class TState> StateRemoteStatus ActivateSourceSubscriber(const System::DeviceRuntimeIdentity& requester,StateSessionToken session,
                                                                      bool hasBaseline,StateVersion baseline={}) noexcept {
        if(!IsRunning()) return StateRemoteStatus::NotRunning;
        std::shared_lock<System::Synchronization::ReadWriteLock> activity(_lifecycle);
        return IsRunning()?Table<TState>().ActivateSubscriber(requester,session,hasBaseline,baseline):StateRemoteStatus::NotRunning;
    }
    template<class TState> StateRemoteStatus AcceptSourceBaseline(const System::DeviceRuntimeIdentity& requester,StateSessionToken session,
                                                                  StateVersion version) noexcept {
        if(!IsRunning()) return StateRemoteStatus::NotRunning;
        std::shared_lock<System::Synchronization::ReadWriteLock> activity(_lifecycle);
        if(!IsRunning()) return StateRemoteStatus::NotRunning;
        const auto status=Table<TState>().AcceptSubscriberBaseline(requester,session,version);
        if(status==StateRemoteStatus::Success) StateTypeRuntime<TState>::Get().NotifyOutboundWork();
        return status;
    }
    template<class TState> StateRemoteStatus RequireSourceResync(const System::DeviceIdentifier& requester) noexcept {
        if(!IsRunning()) return StateRemoteStatus::NotRunning;
        std::shared_lock<System::Synchronization::ReadWriteLock> activity(_lifecycle);
        if(!IsRunning()) return StateRemoteStatus::NotRunning;
        const auto status=Table<TState>().RequireSubscriberResync(requester);
        if(status==StateRemoteStatus::Success) StateTypeRuntime<TState>::Get().NotifyOutboundWork();
        return status;
    }
    template<class TState> bool SourceSubscriberDirty(const System::DeviceIdentifier& requester) const noexcept { return Table<TState>().SubscriberDirty(requester); }
    template<class TState> StateRemoteSessionState GetSourceSubscriberStatus(const System::DeviceIdentifier& requester) const noexcept { return Table<TState>().SubscriberState(requester); }
    template<class TState> StateVersion SourceAcceptedBaseline(const System::DeviceIdentifier& requester) const noexcept { return Table<TState>().SubscriberAcceptedBaseline(requester); }
    template<class TState> std::size_t SubscribersInUse() const noexcept { return Table<TState>().SubscribersInUse(); }

    /// <summary>Captures exact trusted-lineage correlation for deferred adapter gap feedback.</summary>
    template<class TState>
    StateRemoteStatus CaptureContinuity(const System::DeviceIdentifier& peer,StateContinuitySide side,
                                       StateContinuityHandle& output) const noexcept {
        if(!IsRunning()) return StateRemoteStatus::NotRunning;
        std::shared_lock<System::Synchronization::ReadWriteLock> activity(_lifecycle,std::try_to_lock);
        if(!activity) return StateRemoteStatus::Busy;
        if(!IsRunning()) return StateRemoteStatus::NotRunning;
        System::DeviceRuntimeIdentity local{};
        if(!System::RuntimeIdentity::TryRead(local)) return StateRemoteStatus::InvalidIdentity;
        return Table<TState>().CaptureContinuity(local,peer,side,output);
    }
    /// <summary>Invalidates only the exact captured lineage and wakes bounded resync service.</summary>
    template<class TState>
    StateRemoteStatus ReportContinuityLoss(const StateContinuityHandle& handle) noexcept {
        if(!IsRunning()) return StateRemoteStatus::NotRunning;
        std::shared_lock<System::Synchronization::ReadWriteLock> activity(_lifecycle,std::try_to_lock);
        if(!activity) return StateRemoteStatus::Busy;
        if(!IsRunning()) return StateRemoteStatus::NotRunning;
        System::DeviceRuntimeIdentity local{};
        if(!System::RuntimeIdentity::TryRead(local)) return StateRemoteStatus::InvalidIdentity;
        if((handle.Side==StateContinuitySide::RemoteOwner && handle.Requester!=local) ||
           (handle.Side==StateContinuitySide::SourceSubscriber && handle.Owner!=local)) return StateRemoteStatus::InvalidIdentity;
        const auto status=Table<TState>().InvalidateContinuity(handle);
        if(status==StateRemoteStatus::Success) StateTypeRuntime<TState>::Get().NotifyOutboundWork();
        return status;
    }

    /// <summary>Offers at most one subscriber's current truth to the frozen adapter.</summary>
    /// <remarks>The caller is the family/adapter service context. State captures one consistent
    /// snapshot/version before selecting work. A newer Set leaves the slot dirty, so completion of
    /// this older transfer cannot erase the newer authoritative truth.</remarks>
    template<class TState>
    StateTransportAdmission ServiceLatest() noexcept {
        if(!IsRunning()) return {StateTransportAdmissionStatus::Quiesced};
        std::shared_lock<System::Synchronization::ReadWriteLock> activity(_lifecycle,std::try_to_lock);
        if(!activity) return {StateTransportAdmissionStatus::CapacityUnavailable};
        if(!IsRunning()) return {StateTransportAdmissionStatus::Quiesced};
        System::DeviceRuntimeIdentity owner{};
        if(!System::RuntimeIdentity::TryRead(owner)) return {StateTransportAdmissionStatus::InvalidDestination};
        StateRemoteResyncWork remoteWork{};
        const auto remoteReady=Table<TState>().TryPrepareRemoteResync(remoteWork);
        if(remoteReady==StateRemoteStatus::Success) {
            StateOutboundMessage<TState> request{};
            request.Kind=StateMessageKind::ResyncRequest;request.Owner=remoteWork.Owner;request.Requester=owner;
            request.Session=remoteWork.Session;request.Resync=remoteWork.Resync;
            const auto admitted=StateTypeRuntime<TState>::Get().AdmitOutbound(request);
            Table<TState>().CompleteRemoteResyncTransfer(remoteWork,bool(admitted));
            return admitted;
        }
        if(remoteReady!=StateRemoteStatus::NotFound) return {StateTransportAdmissionStatus::CapacityUnavailable};
        StateSnapshot<TState> snapshot{};StateVersion version{};
        if(!StateTypeRuntime<TState>::Get().TryReadVersioned(snapshot,version))
            return {StateTransportAdmissionStatus::CapacityUnavailable};
        StateSourceWork work{};
        if(!Table<TState>().TryPrepareLatest(version,snapshot,work))
            return {StateTransportAdmissionStatus::CapacityUnavailable};
        StateOutboundMessage<TState> message{};
        message.Kind=work.Kind;message.Owner=owner;message.Requester=work.Requester;
        message.Session=work.Session;message.Version=work.Version;
        message.Snapshot=snapshot;message.HasSnapshot=work.Kind!=StateMessageKind::ResyncRequired;
        const auto admitted=StateTypeRuntime<TState>::Get().AdmitOutbound(message);
        Table<TState>().CompleteLatestTransfer(work,bool(admitted));
        return admitted;
    }

    /// <summary>Decodes, validates and commits one Type-specific remote State message.</summary>
    /// <remarks>The borrowed bytes are fully decoded before mutation. Success means the destination
    /// State-family boundary has committed the applicable bounded session/replica state. Required
    /// replies are offered only after that commit and may return TemporarilyUnavailable for retry.
    /// All framework locks in this boundary are single nonblocking attempts against storage
    /// resolved during Initialize. A Busy result never means NoValue or token exhaustion.</remarks>
    template<class TState,class Format>
    StateRemoteAdmissionResult AdmitRemote(const std::uint8_t* data,std::size_t size,
                                           StateValidatedIngressContext ingress) noexcept {
        using D=Primitive::PrimitiveAdmissionDisposition;
        if(!IsRunning()) return {D::TemporarilyUnavailable,StateWireStatus::Success};
        std::shared_lock<System::Synchronization::ReadWriteLock> activity(_lifecycle,std::try_to_lock);
        if(!activity || !IsRunning()) return {D::TemporarilyUnavailable,StateWireStatus::Success};
        StateDecodedIngress<TState> decoded{};
        auto parsed=DecodeValidatedStateIngress<TState,Format>(data,size,ingress,decoded);
        if(!parsed) return parsed;
        System::DeviceRuntimeIdentity local{};
        if(!System::RuntimeIdentity::TryRead(local)) return {D::Rejected,StateWireStatus::Success};

        const bool fromOwner=StateMessageSourceRole(decoded.Kind)==StateSemanticSourceRole::Owner;
        if((fromOwner && decoded.Requester!=local) ||
           (!fromOwner && (decoded.Owner.Device!=local.Device ||
                           (decoded.Kind!=StateMessageKind::SubscribeRequest && decoded.Owner!=local))))
            return {D::Rejected,StateWireStatus::Success};

        StateOutboundMessage<TState> reply{};
        reply.Owner=fromOwner?decoded.Owner:local;
        reply.Requester=fromOwner?local:decoded.Requester;
        reply.Session=decoded.Session;
        StateRemoteStatus status=StateRemoteStatus::Conflict;

        switch(decoded.Kind) {
            case StateMessageKind::SubscribeRequest: {
                status=Table<TState>().template ReserveSubscriber<true>(decoded.Requester,decoded.Session);
                if(status!=StateRemoteStatus::Success && status!=StateRemoteStatus::Duplicate)
                    return {MapRemoteAdmission(status),StateWireStatus::Success};
                StateSnapshot<TState> snapshot{};StateVersion version{};
                const auto captured=StateTypeRuntime<TState>::Get().TryCaptureVersioned(snapshot,version);
                if(captured==Detail::StateCaptureStatus::Busy) return {D::TemporarilyUnavailable,StateWireStatus::Success};
                bool hasValue=captured==Detail::StateCaptureStatus::Success;
                const auto offered=Table<TState>().template PrepareSubscriberEstablishment<true>(decoded.Requester,decoded.Session,hasValue,version,snapshot);
                if(offered!=StateRemoteStatus::Success) return {MapRemoteAdmission(offered),StateWireStatus::Success};
                reply.Kind=hasValue?StateMessageKind::SubscribeSnapshot:StateMessageKind::SubscribeNoValue;
                reply.Version=version;reply.Snapshot=snapshot;reply.HasSnapshot=hasValue;
                return {OfferReply(reply),StateWireStatus::Success};
            }
            case StateMessageKind::SubscribeSnapshot:
                status=Table<TState>().template InstallSubscribeSnapshot<true>(decoded.Owner,decoded.Session,decoded.Version,decoded.Snapshot);
                if(status==StateRemoteStatus::Success || status==StateRemoteStatus::Duplicate) {
                    reply.Kind=StateMessageKind::SubscribeAccepted;
                    const auto out=OfferReply(reply);
                    return {out==D::Accepted?MapRemoteAdmission(status):out,StateWireStatus::Success};
                }
                break;
            case StateMessageKind::SubscribeNoValue:
                status=Table<TState>().template InstallSubscribeNoValue<true>(decoded.Owner,decoded.Session);
                if(status==StateRemoteStatus::Success || status==StateRemoteStatus::Duplicate) {
                    reply.Kind=StateMessageKind::SubscribeAccepted;
                    const auto out=OfferReply(reply);
                    return {out==D::Accepted?MapRemoteAdmission(status):out,StateWireStatus::Success};
                }
                break;
            case StateMessageKind::SubscribeAccepted: {
                StateSnapshot<TState> snapshot{};StateVersion version{};
                if(StateTypeRuntime<TState>::Get().TryCaptureVersioned(snapshot,version)==Detail::StateCaptureStatus::Busy)
                    return {D::TemporarilyUnavailable,StateWireStatus::Success};
                status=Table<TState>().template AcceptSubscriberEstablishment<true>(decoded.Requester,decoded.Session,
                                                                      version);
                break;
            }
            case StateMessageKind::SubscribeRejected: {
                std::unique_lock<System::Synchronization::Mutex> lock(_tokenMutex,std::try_to_lock);
                if(!lock) return {D::TemporarilyUnavailable,StateWireStatus::Success};
                const StateSubscriptionHandle handle{TState::TypeId,decoded.Owner.Device,decoded.Session};
                auto& selector=Selector<TState>();
                if(!selector.Contains(handle)) { status=StateRemoteStatus::SessionMismatch;break; }
                status=Table<TState>().template RejectSubscription<true>(decoded.Owner.Device,decoded.Session);
                if(status==StateRemoteStatus::Success) {
                    (void)selector.Remove(handle);
                    if(selector.Count==0 && selector.InFlight==0) selector.Mode=StateSubscriptionSelectorMode::None;
                }
                break;
            }
            case StateMessageKind::UnsubscribeRequest:
                status=Table<TState>().template RemoveSubscriber<true>(decoded.Requester,decoded.Session);
                break;
            case StateMessageKind::BaselineSnapshot:
                status=Table<TState>().template InstallBaselineSnapshot<true>(decoded.Owner,decoded.Session,decoded.Version,decoded.Snapshot);
                if(status==StateRemoteStatus::Success || status==StateRemoteStatus::Duplicate) {
                    reply.Kind=StateMessageKind::BaselineAccepted;reply.Version=decoded.Version;
                    const auto out=OfferReply(reply);
                    return {out==D::Accepted?MapRemoteAdmission(status):out,StateWireStatus::Success};
                }
                break;
            case StateMessageKind::BaselineAccepted:
                status=Table<TState>().template AcceptSubscriberBaseline<true>(decoded.Requester,decoded.Session,decoded.Version);
                break;
            case StateMessageKind::Publication:
                status=Table<TState>().template ApplyPublication<true>(decoded.Owner,decoded.Session,decoded.Version,decoded.Snapshot);
                if constexpr(StateRemoteReplicaTable<TState,
                    Detail::ConfigurationForT<TState,TConfigurations...>::RemoteOwnerCapacity,
                    Detail::ConfigurationForT<TState,TConfigurations...>::SubscriberCapacity>::RequiresAcknowledgement) {
                    if(status==StateRemoteStatus::Success || status==StateRemoteStatus::Duplicate || status==StateRemoteStatus::Older) {
                        reply.Kind=StateMessageKind::PublicationAccepted;reply.Version=decoded.Version;
                        const auto out=OfferReply(reply);
                        return {out==D::Accepted?MapRemoteAdmission(status):out,StateWireStatus::Success};
                    }
                }
                break;
            case StateMessageKind::PublicationAccepted:
                status=Table<TState>().template AcceptSubscriberBaseline<true>(decoded.Requester,decoded.Session,decoded.Version);
                break;
            case StateMessageKind::ResyncRequired: {
                StateResyncToken token{};
                status=Table<TState>().template BeginRemoteResync<true>(decoded.Owner,decoded.Session,token);
                if(status==StateRemoteStatus::TokenExhausted)
                    return {D::ResourceUnavailable,StateWireStatus::Success};
                if(status!=StateRemoteStatus::Success) break;
                reply.Kind=StateMessageKind::ResyncRequest;reply.Resync=token;
                return {OfferReply(reply),StateWireStatus::Success};
            }
            case StateMessageKind::ResyncRequest: {
                StateSnapshot<TState> snapshot{};StateVersion version{};
                const auto captured=StateTypeRuntime<TState>::Get().TryCaptureVersioned(snapshot,version);
                if(captured==Detail::StateCaptureStatus::Busy) return {D::TemporarilyUnavailable,StateWireStatus::Success};
                if(captured==Detail::StateCaptureStatus::NoValue) return {D::Rejected,StateWireStatus::Success};
                status=Table<TState>().template PrepareSubscriberResync<true>(decoded.Requester,decoded.Session,decoded.Resync,version,snapshot);
                if(status!=StateRemoteStatus::Success) break;
                reply.Kind=StateMessageKind::ResyncSnapshot;reply.Resync=decoded.Resync;
                reply.Version=version;reply.Snapshot=snapshot;reply.HasSnapshot=true;
                return {OfferReply(reply),StateWireStatus::Success};
            }
            case StateMessageKind::ResyncSnapshot:
                status=Table<TState>().template InstallResyncSnapshot<true>(decoded.Owner,decoded.Session,decoded.Resync,decoded.Version,decoded.Snapshot);
                if(status==StateRemoteStatus::Success || status==StateRemoteStatus::Duplicate) {
                    reply.Kind=StateMessageKind::ResyncAccepted;reply.Resync=decoded.Resync;reply.Version=decoded.Version;
                    const auto out=OfferReply(reply);
                    return {out==D::Accepted?MapRemoteAdmission(status):out,StateWireStatus::Success};
                }
                break;
            case StateMessageKind::ResyncAccepted: {
                StateSnapshot<TState> snapshot{};StateVersion version{};
                if(StateTypeRuntime<TState>::Get().TryCaptureVersioned(snapshot,version)==Detail::StateCaptureStatus::Busy)
                    return {D::TemporarilyUnavailable,StateWireStatus::Success};
                status=Table<TState>().template AcceptSubscriberResync<true>(decoded.Requester,decoded.Session,decoded.Resync,decoded.Version,
                                                               version);
                break;
            }
        }
        if(status==StateRemoteStatus::Success &&
           (decoded.Kind==StateMessageKind::SubscribeAccepted || decoded.Kind==StateMessageKind::BaselineAccepted ||
            decoded.Kind==StateMessageKind::PublicationAccepted || decoded.Kind==StateMessageKind::ResyncAccepted))
            StateTypeRuntime<TState>::Get().NotifyOutboundWork();
        return {MapRemoteAdmission(status),StateWireStatus::Success};
    }

    bool IsRunning() const noexcept { return _running.load(std::memory_order_acquire) && !_closing.load(std::memory_order_acquire); }
};
}
