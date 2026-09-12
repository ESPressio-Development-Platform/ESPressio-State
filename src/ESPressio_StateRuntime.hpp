#pragma once
#include <type_traits>
#include <ESPressio_TypeDirectory.hpp>
#include "ESPressio_StateDescriptor.hpp"
#include "ESPressio_StatePersistence.hpp"
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
}

template<class... TConfigurations>
class Runtime final {
    bool _initialized=false;
    bool _running=false;
    bool _configurationError=false;
    template<class C>
    static StateRuntimeStatus InitializeOne(Primitive::TypeDirectoryView directory,Timing::QualifiedTime(*capture)()) noexcept {
        using T=typename C::StateType;
        const auto* common=directory.Find({StateFamilyId,T::TypeId.Value()});
        if(!common) return StateRuntimeStatus::InvalidDirectory;
        const auto* descriptor=GetStateTypeDescriptor(*common);
        if(!descriptor || descriptor->TypeId!=T::TypeId) return StateRuntimeStatus::TypeConflict;
        return StateTypeRuntime<T>::Get().Initialize(capture);
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
        if(_initialized){return {};}
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
    bool IsRunning() const noexcept { return _running; }
};
}
