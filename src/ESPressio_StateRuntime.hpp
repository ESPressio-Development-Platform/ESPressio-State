#pragma once
#include <array>
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
        if(!owner || self.Count==Capacity) return false;
        for(std::size_t i=0;i<self.Count;++i) if(self.Owners[i]==owner) return true;
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
    bool _running=false;
    bool _configurationError=false;
    RemoteTables _remote{};
    SelectorStates _selectors{};
    mutable System::Synchronization::Mutex _tokenMutex;
    StateSessionTokenGenerator _sessionTokens{};
    StateResyncTokenGenerator _resyncTokens{};

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
    static StateRuntimeStatus InitializeOne(Primitive::TypeDirectoryView directory,Timing::QualifiedTime(*capture)()) noexcept {
        using T=typename C::StateType;
        const auto* common=directory.Find({StateFamilyId,T::TypeId.Value()});
        if(!common) return StateRuntimeStatus::InvalidDirectory;
        const auto* descriptor=GetStateTypeDescriptor(*common);
        if(!descriptor || descriptor->TypeId!=T::TypeId) return StateRuntimeStatus::TypeConflict;
        return StateTypeRuntime<T>::Get().Initialize(capture);
    }
    template<class C>
    StateRuntimeStatus BindConvergenceOne() noexcept {
        if constexpr(C::SubscriberCapacity==0) return StateRuntimeStatus::Success;
        else return StateTypeRuntime<typename C::StateType>::Get().BindConvergence(Table<typename C::StateType>().ConvergenceView());
    }
    template<class C> static void RollbackOne() noexcept { (void)StateTypeRuntime<typename C::StateType>::Get().RollbackInitialization(); }
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
    template<class TState>
    StateSubscriptionResult StartConcreteSubscription(const System::DeviceIdentifier& owner,StateSubscriptionSelectorMode mode) noexcept {
        using C=Detail::ConfigurationForT<TState,TConfigurations...>;
        static_assert(!std::is_void_v<C> && TState::IsTransmissibleState);
        static_assert(C::RemoteOwnerCapacity>0,"Remote State subscription requires MaximumRemoteOwners > 0");
        if(!_running) return {StateRemoteStatus::NotRunning,{}};
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
            if(!_sessionTokens.TryAllocate(token)) {
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
            if(hadRetained) (void)Table<TState>().Unsubscribe(owner,StateReplicaRelease::RetainLastKnown);
            else (void)Table<TState>().ForgetRemote(owner);
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
    Runtime(const Runtime&)=delete;
    Runtime& operator=(const Runtime&)=delete;

    template<class TState>
    StateOwner<TState> BindOwner() noexcept {
        using C=Detail::ConfigurationForT<TState,TConfigurations...>;
        static_assert(!std::is_void_v<C>,"State Type is not configured in this Runtime");
        if(_initialized) return {};
        auto owner=StateTypeRuntime<TState>::Get().BindOwner();
        if(!owner) _configurationError=true;
        return owner;
    }
    template<class TState,class Format>
    StateRuntimeStatus BindPersistence(StatePersistenceBinding<TState,Format>& binding) noexcept {
        using C=Detail::ConfigurationForT<TState,TConfigurations...>;
        static_assert(!std::is_void_v<C>,"State Type is not configured in this Runtime");
        if(_initialized) return StateRuntimeStatus::Frozen;
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
        const auto status=StateTypeRuntime<TState>::Get().BindTransport(binding.View());
        if(status!=StateRuntimeStatus::Success) _configurationError=true;
        return status;
    }
    StateRuntimeStatus Initialize(Primitive::TypeDirectoryView directory,Timing::QualifiedTime(*capture)()=nullptr) noexcept {
        if(_initialized) return StateRuntimeStatus::AlreadyInitialized;
        if(_configurationError) return StateRuntimeStatus::InvalidConfiguration;
        if(!directory.IsFrozen()) return StateRuntimeStatus::InvalidDirectory;
        StateRuntimeStatus status=StateRuntimeStatus::Success;
        bool ok=true;
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
        if(!_initialized) return StateRuntimeStatus::NotInitialized;
        if(_running) return StateRuntimeStatus::Frozen;
        if(!(ValidateOne<TConfigurations>() && ...)) return StateRuntimeStatus::InvalidConfiguration;
        (StateTypeRuntime<typename TConfigurations::StateType>::Get().StartValidated(),...);
        _running=true;
        return StateRuntimeStatus::Success;
    }
    StateRuntimeStatus Shutdown() noexcept {
        if(!_initialized) return StateRuntimeStatus::NotInitialized;
        (StateTypeRuntime<typename TConfigurations::StateType>::Get().Shutdown(),...);
        _running=false;
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
        using C=Detail::ConfigurationForT<TState,TConfigurations...>;
        static_assert(!std::is_void_v<C> && TState::IsTransmissibleState);
        if(!_running) return {StateRemoteStatus::NotRunning,{}};
        StateSessionToken token{};
        {
            std::lock_guard<System::Synchronization::Mutex> lock(_tokenMutex);
            if(!_sessionTokens.TryAllocate(token)) return {StateRemoteStatus::TokenExhausted,{}};
        }
        const auto status=Table<TState>().ReserveRemoteOwner(owner,token);
        if(status!=StateRemoteStatus::Success) return {status,{}};
        return {StateRemoteStatus::Success,{TState::TypeId,owner,token}};
    }

    /// <summary>Starts one concrete remote-owner session and emits a semantic SubscribeRequest.</summary>
    template<class TState>
    StateSubscriptionResult SubscribeFrom(const System::DeviceIdentifier& owner) noexcept {
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
        if(!_running) { result.Status=StateRemoteStatus::NotRunning; return result; }
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
        {
            std::lock_guard<System::Synchronization::Mutex> lock(_tokenMutex);
            if(Selector<TState>().Mode!=StateSubscriptionSelectorMode::AnyDevice) return {StateRemoteStatus::Conflict,{}};
        }
        return StartConcreteSubscription<TState>(owner,StateSubscriptionSelectorMode::AnyDevice);
    }

    template<class TState>
    StateRemoteStatus Unsubscribe(const StateSubscriptionHandle& handle,StateReplicaRelease disposition) noexcept {
        if(!_running) return StateRemoteStatus::NotRunning;
        if(handle.TypeId!=TState::TypeId || !handle) return StateRemoteStatus::InvalidSession;
        {
            std::lock_guard<System::Synchronization::Mutex> lock(_tokenMutex);
            if(!Selector<TState>().Contains(handle)) return StateRemoteStatus::SessionMismatch;
        }
        System::DeviceRuntimeIdentity requester{};
        if(!System::RuntimeIdentity::TryRead(requester)) return StateRemoteStatus::InvalidIdentity;
        StateOutboundMessage<TState> request{};
        request.Kind=StateMessageKind::UnsubscribeRequest;
        request.Owner={handle.OwnerDevice,{}};
        request.Requester=requester;
        request.Session=handle.Session;
        const auto admitted=StateTypeRuntime<TState>::Get().AdmitOutbound(request);
        if(!admitted) return MapTransport(admitted.Status);
        const auto status=Table<TState>().Unsubscribe(handle.OwnerDevice,disposition);
        if(status!=StateRemoteStatus::Success) return status;
        {
            std::lock_guard<System::Synchronization::Mutex> lock(_tokenMutex);
            auto& selector=Selector<TState>();
            if(!selector.Remove(handle)) return StateRemoteStatus::SessionMismatch;
            if(selector.Count==0 && selector.InFlight==0) selector.Mode=StateSubscriptionSelectorMode::None;
        }
        return StateRemoteStatus::Success;
    }

    template<class TState>
    StateRemoteStatus StopAny() noexcept {
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
        std::lock_guard<System::Synchronization::Mutex> lock(_tokenMutex);
        return _resyncTokens.TryAllocate(output)?StateRemoteStatus::Success:StateRemoteStatus::TokenExhausted;
    }

    template<class TState> StateRemoteStatus InstallSubscribeSnapshot(const System::DeviceRuntimeIdentity& owner,StateSessionToken session,
                                                                       StateVersion version,const StateSnapshot<TState>& snapshot) noexcept {
        return _running?Table<TState>().InstallSubscribeSnapshot(owner,session,version,snapshot):StateRemoteStatus::NotRunning;
    }
    template<class TState> StateRemoteStatus InstallSubscribeNoValue(const System::DeviceRuntimeIdentity& owner,StateSessionToken session) noexcept {
        return _running?Table<TState>().InstallSubscribeNoValue(owner,session):StateRemoteStatus::NotRunning;
    }
    template<class TState> StateRemoteStatus InstallBaselineSnapshot(const System::DeviceRuntimeIdentity& owner,StateSessionToken session,
                                                                     StateVersion version,const StateSnapshot<TState>& snapshot) noexcept {
        return _running?Table<TState>().InstallBaselineSnapshot(owner,session,version,snapshot):StateRemoteStatus::NotRunning;
    }
    template<class TState> StateRemoteStatus ApplyRemotePublication(const System::DeviceRuntimeIdentity& owner,StateSessionToken session,
                                                                    StateVersion version,const StateSnapshot<TState>& snapshot) noexcept {
        return _running?Table<TState>().ApplyPublication(owner,session,version,snapshot):StateRemoteStatus::NotRunning;
    }
    template<class TState> StateRemoteStatus RequireRemoteResync(const System::DeviceIdentifier& owner) noexcept {
        return _running?Table<TState>().RequireResync(owner):StateRemoteStatus::NotRunning;
    }
    template<class TState> StateRemoteStatus BeginRemoteResync(const System::DeviceIdentifier& owner,StateResyncToken token) noexcept {
        return _running?Table<TState>().BeginResync(owner,token):StateRemoteStatus::NotRunning;
    }
    template<class TState> StateRemoteStatus InstallResyncSnapshot(const System::DeviceRuntimeIdentity& owner,StateSessionToken session,
                                                                   StateResyncToken token,StateVersion version,const StateSnapshot<TState>& snapshot) noexcept {
        return _running?Table<TState>().InstallResyncSnapshot(owner,session,token,version,snapshot):StateRemoteStatus::NotRunning;
    }
    template<class TState> bool TryReadRemote(const System::DeviceIdentifier& owner,StateSnapshot<TState>& output) const noexcept {
        return Table<TState>().TryReadRemote(owner,output);
    }
    template<class TState> StateRemoteSessionState GetRemoteSessionStatus(const System::DeviceIdentifier& owner) const noexcept {
        return Table<TState>().SessionState(owner);
    }
    template<class TState> StateRemoteStatus Unsubscribe(const System::DeviceIdentifier& owner,StateReplicaRelease disposition) noexcept {
        return _running?Table<TState>().Unsubscribe(owner,disposition):StateRemoteStatus::NotRunning;
    }
    template<class TState> StateRemoteStatus ForgetRemote(const System::DeviceIdentifier& owner) noexcept {
        return _running?Table<TState>().ForgetRemote(owner):StateRemoteStatus::NotRunning;
    }
    template<class TState> std::size_t RemoteOwnersInUse() const noexcept { return Table<TState>().RemoteOwnersInUse(); }

    template<class TState> StateRemoteStatus ReserveSourceSubscriber(const System::DeviceRuntimeIdentity& requester,StateSessionToken session) noexcept {
        return _running?Table<TState>().ReserveSubscriber(requester,session):StateRemoteStatus::NotRunning;
    }
    template<class TState> StateRemoteStatus ActivateSourceSubscriber(const System::DeviceRuntimeIdentity& requester,StateSessionToken session,
                                                                      bool hasBaseline,StateVersion baseline={}) noexcept {
        return _running?Table<TState>().ActivateSubscriber(requester,session,hasBaseline,baseline):StateRemoteStatus::NotRunning;
    }
    template<class TState> StateRemoteStatus AcceptSourceBaseline(const System::DeviceRuntimeIdentity& requester,StateSessionToken session,
                                                                  StateVersion version) noexcept {
        return _running?Table<TState>().AcceptSubscriberBaseline(requester,session,version):StateRemoteStatus::NotRunning;
    }
    template<class TState> StateRemoteStatus RequireSourceResync(const System::DeviceIdentifier& requester) noexcept {
        return _running?Table<TState>().RequireSubscriberResync(requester):StateRemoteStatus::NotRunning;
    }
    template<class TState> bool SourceSubscriberDirty(const System::DeviceIdentifier& requester) const noexcept { return Table<TState>().SubscriberDirty(requester); }
    template<class TState> StateRemoteSessionState GetSourceSubscriberStatus(const System::DeviceIdentifier& requester) const noexcept { return Table<TState>().SubscriberState(requester); }
    template<class TState> StateVersion SourceAcceptedBaseline(const System::DeviceIdentifier& requester) const noexcept { return Table<TState>().SubscriberAcceptedBaseline(requester); }
    template<class TState> std::size_t SubscribersInUse() const noexcept { return Table<TState>().SubscribersInUse(); }

    bool IsRunning() const noexcept { return _running; }
};
}
