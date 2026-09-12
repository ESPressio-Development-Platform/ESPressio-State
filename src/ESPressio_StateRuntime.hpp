#pragma once
#include <tuple>
#include <type_traits>
#include <ESPressio_Synchronization.hpp>
#include <ESPressio_TypeDirectory.hpp>
#include "ESPressio_StateDescriptor.hpp"
#include "ESPressio_StatePersistence.hpp"
#include "ESPressio_StateRemoteReplica.hpp"
#include "ESPressio_StateRuntimeConfiguration.hpp"
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
}

template<class... TConfigurations>
class Runtime final {
    static_assert(sizeof...(TConfigurations)>0,"State Runtime requires at least one configured Type");
    static_assert((Detail::ValidateDeploymentConfiguration<TConfigurations>() && ...));
    using RemoteTables=std::tuple<StateRemoteReplicaTable<typename TConfigurations::StateType,
                                                          TConfigurations::RemoteOwnerCapacity,
                                                          TConfigurations::SubscriberCapacity>...>;
    bool _initialized=false;
    bool _running=false;
    bool _configurationError=false;
    RemoteTables _remote{};
    mutable System::Synchronization::Mutex _tokenMutex;
    StateSessionTokenGenerator _sessionTokens{};
    StateResyncTokenGenerator _resyncTokens{};

    template<class TState>
    auto& Table() noexcept {
        static_assert(Detail::ConfigurationCount<TState,TConfigurations...>==1,"State Type must appear exactly once in Runtime configuration");
        using C=Detail::ConfigurationForT<TState,TConfigurations...>;
        using TableType=StateRemoteReplicaTable<TState,C::RemoteOwnerCapacity,C::SubscriberCapacity>;
        return std::get<TableType>(_remote);
    }
    template<class TState>
    const auto& Table() const noexcept {
        static_assert(Detail::ConfigurationCount<TState,TConfigurations...>==1,"State Type must appear exactly once in Runtime configuration");
        using C=Detail::ConfigurationForT<TState,TConfigurations...>;
        using TableType=StateRemoteReplicaTable<TState,C::RemoteOwnerCapacity,C::SubscriberCapacity>;
        return std::get<TableType>(_remote);
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
public:
    Runtime() noexcept=default;
    Runtime(const Runtime&)=delete;Runtime& operator=(const Runtime&)=delete;

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
        _initialized=true;return StateRuntimeStatus::Success;
    }
    StateRuntimeStatus Start() noexcept {
        if(!_initialized) return StateRuntimeStatus::NotInitialized;
        if(_running) return StateRuntimeStatus::Frozen;
        if(!(ValidateOne<TConfigurations>() && ...)) return StateRuntimeStatus::InvalidConfiguration;
        (StateTypeRuntime<typename TConfigurations::StateType>::Get().StartValidated(),...);
        _running=true;return StateRuntimeStatus::Success;
    }
    StateRuntimeStatus Shutdown() noexcept {
        if(!_initialized) return StateRuntimeStatus::NotInitialized;
        (StateTypeRuntime<typename TConfigurations::StateType>::Get().Shutdown(),...);
        _running=false;return StateRuntimeStatus::Success;
    }

    template<class TState> bool TryRead(StateSnapshot<TState>& output) const noexcept {
        using C=Detail::ConfigurationForT<TState,TConfigurations...>;
        static_assert(!std::is_void_v<C>,"State Type is not configured in this Runtime");
        return StateTypeRuntime<TState>::Get().TryRead(output);
    }
    template<class TState> StateVersion Version() const noexcept { return StateTypeRuntime<TState>::Get().Version(); }
    template<class TState> static constexpr std::size_t MaximumRemoteOwnersFor() noexcept {
        using C=Detail::ConfigurationForT<TState,TConfigurations...>;static_assert(!std::is_void_v<C>);return C::RemoteOwnerCapacity;
    }
    template<class TState> static constexpr std::size_t MaximumSubscribersFor() noexcept {
        using C=Detail::ConfigurationForT<TState,TConfigurations...>;static_assert(!std::is_void_v<C>);return C::SubscriberCapacity;
    }

    /// <summary>Reserves requester-side bounded session state; adapter emission is a separate family-binding step.</summary>
    template<class TState>
    StateSubscriptionResult ReserveSubscriptionSession(const System::DeviceIdentifier& owner) noexcept {
        using C=Detail::ConfigurationForT<TState,TConfigurations...>;static_assert(!std::is_void_v<C> && TState::IsTransmissibleState);
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
