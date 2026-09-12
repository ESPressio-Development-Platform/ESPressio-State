#pragma once
#include <atomic>
#include <cstdint>
#include <mutex>
#include <type_traits>
#include <ESPressio_RuntimeIdentity.hpp>
#include <ESPressio_SystemClock.hpp>
#include <ESPressio_Synchronization.hpp>
#include "ESPressio_StateCanonicalStorage.hpp"
#include "ESPressio_StateComparison.hpp"
#include "ESPressio_StateOwner.hpp"
#include "ESPressio_StateSnapshot.hpp"
#include "ESPressio_StateVersion.hpp"

namespace ESPressio::State {
namespace Detail {
template<class T> constexpr bool ValidateLocalStateType() noexcept {
    static_assert(std::is_same_v<std::remove_cv_t<decltype(T::TypeId)>,StateTypeId>,"State TypeId must be strong StateTypeId");
    static_assert(bool(T::TypeId),"State TypeId must be nonzero");
    static_assert(HasCanonicalName<T>::value,"State Type requires static CanonicalName");
    static_assert(StateStorageTraits<typename T::ValueType>::Supported,
                  "State Value requires deterministic bounded StateStorageTraits");
    static_assert(ValidateStateComparison<T>());
    return true;
}
}

template<class TState>
class StateTypeRuntime final {
    static_assert(Detail::ValidateLocalStateType<TState>());
    using Value=typename TState::ValueType;
    enum class Phase:std::uint8_t{Uninitialized,Prepared,Running,Stopping,Stopped};
    mutable System::Synchronization::Mutex _mutex;
    StateCanonicalStorage<Value> _storage{};
    Timing::QualifiedTime _truthTime{};
    StateVersion _version{};
    bool _hasValue=false;
    bool _ownerEverBound=false;
    bool _ownerAlive=false;
    std::uint64_t _ownerToken=0;
    std::atomic<Phase> _phase{Phase::Uninitialized};
    Timing::QualifiedTime (*_captureTime)()=nullptr;

    StateTypeRuntime() noexcept=default;
    static Timing::QualifiedTime CaptureSystemTime(){ return Timing::SystemClock<>::GetInstance().CaptureQualifiedTime(); }
    void ReleaseOwner(std::uint64_t token) noexcept {
        std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
        if(token && token==_ownerToken) _ownerAlive=false;
    }
    StateSetStatus Set(std::uint64_t token,const Value& candidate,Timing::QualifiedTime truthTime) {
        if(_phase.load(std::memory_order_acquire)!=Phase::Running) return StateSetStatus::NotRunning;
        Value prepared{};
        if(!_storage.Prepare(candidate,prepared)) return StateSetStatus::StoragePreparationFailed;
        std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
        if(_phase.load(std::memory_order_relaxed)!=Phase::Running) return StateSetStatus::NotRunning;
        if(!token || token!=_ownerToken || !_ownerAlive) return StateSetStatus::OwnerUnavailable;
        if(_hasValue){
            Value current{};
            _storage.CopyOut(current);
            if(StateComparison<TState>::Equals(current,prepared)) return StateSetStatus::NoChange;
        }
        const auto next=NextStateVersion(_version,_hasValue);
        _storage.CommitPrepared(prepared);
        _truthTime=truthTime;
        _version=next;
        _hasValue=true;
        return StateSetStatus::Changed;
    }
    friend class StateOwner<TState>;
    template<class...> friend class Runtime;
public:
    StateTypeRuntime(const StateTypeRuntime&)=delete;
    StateTypeRuntime& operator=(const StateTypeRuntime&)=delete;
    static StateTypeRuntime& Get() noexcept { static StateTypeRuntime instance;return instance; }
    StateOwner<TState> BindOwner() noexcept {
        std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
        if(_phase.load(std::memory_order_relaxed)!=Phase::Uninitialized || _ownerEverBound) return {};
        _ownerEverBound=true;_ownerAlive=true;
        _ownerToken=1;
        return StateOwner<TState>(this,_ownerToken);
    }
    StateRuntimeStatus Initialize(Timing::QualifiedTime(*captureTime)()=nullptr) noexcept {
        std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
        if(_phase.load(std::memory_order_relaxed)!=Phase::Uninitialized) return StateRuntimeStatus::AlreadyInitialized;
        if(!System::RuntimeIdentity::IsInstalled()) return StateRuntimeStatus::IdentityUnavailable;
        _captureTime=captureTime?captureTime:&CaptureSystemTime;
        _phase.store(Phase::Prepared,std::memory_order_release);
        return StateRuntimeStatus::Success;
    }
    void StartValidated() noexcept { _phase.store(Phase::Running,std::memory_order_release); }
    StateRuntimeStatus RollbackInitialization() noexcept {
        std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
        if(_phase.load(std::memory_order_relaxed)==Phase::Prepared){_captureTime=nullptr;_phase.store(Phase::Uninitialized);}
        return StateRuntimeStatus::Success;
    }
    StateRuntimeStatus Shutdown() noexcept {
        auto phase=_phase.load(std::memory_order_acquire);
        if(phase==Phase::Uninitialized || phase==Phase::Stopped) return StateRuntimeStatus::NotInitialized;
        _phase.store(Phase::Stopping,std::memory_order_release);
        _phase.store(Phase::Stopped,std::memory_order_release);
        return StateRuntimeStatus::Success;
    }
    bool IsRunning() const noexcept { return _phase.load(std::memory_order_acquire)==Phase::Running; }
    bool HasValue() const noexcept { std::lock_guard<System::Synchronization::Mutex> lock(_mutex);return _hasValue; }
    bool HasOwner() const noexcept { std::lock_guard<System::Synchronization::Mutex> lock(_mutex);return _ownerEverBound; }
    bool OwnerAlive() const noexcept { std::lock_guard<System::Synchronization::Mutex> lock(_mutex);return _ownerAlive; }
    StateVersion Version() const noexcept { std::lock_guard<System::Synchronization::Mutex> lock(_mutex);return _version; }
    bool TryRead(StateSnapshot<TState>& output) const noexcept {
        std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
        if(!_hasValue) return false;
        _storage.CopyOut(output.Value);output.TruthTime=_truthTime;return true;
    }
};

template<class TState>
void StateOwner<TState>::Release() noexcept {
    auto* runtime=std::exchange(_runtime,nullptr);const auto token=std::exchange(_token,0);
    if(runtime) runtime->ReleaseOwner(token);
}
template<class TState>
StateSetStatus StateOwner<TState>::Set(const typename TState::ValueType& candidate){
    if(!_runtime || !_token) return StateSetStatus::OwnerUnavailable;
    const auto truth=_runtime->_captureTime ? _runtime->_captureTime() : Timing::QualifiedTime{};
    return _runtime->Set(_token,candidate,truth);
}
template<class TState>
StateSetStatus StateOwner<TState>::Set(const typename TState::ValueType& candidate,Timing::QualifiedTime truthTime){
    return (!_runtime || !_token) ? StateSetStatus::OwnerUnavailable : _runtime->Set(_token,candidate,truthTime);
}
}
