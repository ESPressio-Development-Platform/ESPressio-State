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
#include "ESPressio_StateObserverTarget.hpp"
#include "ESPressio_StateOwner.hpp"
#include "ESPressio_StatePersistence.hpp"
#include "ESPressio_StateRemoteReplica.hpp"
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
    bool _restoredDuringInitialize=false;
    std::uint64_t _ownerToken=0;
    StateObserverTargetNode* _observerTargets=nullptr;
    StatePersistenceBindingView<TState> _persistence{};
    StateConvergenceBindingView<TState> _convergence{};
    std::atomic<Phase> _phase{Phase::Uninitialized};
    Timing::QualifiedTime (*_captureTime)()=nullptr;

    StateTypeRuntime() noexcept=default;
    static Timing::QualifiedTime CaptureSystemTime(){ return Timing::SystemClock<>::GetInstance().CaptureQualifiedTime(); }
    void ReleaseOwner(std::uint64_t token) noexcept {
        std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
        if(token && token==_ownerToken) _ownerAlive=false;
    }
    void PublishObserversLocked() noexcept {
        for(auto* target=_observerTargets;target;target=target->Next)
            if(target->Linked.load(std::memory_order_acquire) && target->Publish)
                target->Publish(target->Owner,target->ObservationIndex);
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
        if(_persistence && !_persistence.Commit(_persistence.Owner,prepared,truthTime))
            return StateSetStatus::PersistenceFailed;
        _storage.CommitPrepared(prepared);
        _truthTime=truthTime;
        _version=next;
        _hasValue=true;
        PublishObserversLocked();
        if(_convergence) _convergence.MarkLatestDirty(_convergence.Owner,_version);
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
        _ownerEverBound=true;
        _ownerAlive=true;
        _ownerToken=1;
        return StateOwner<TState>(this,_ownerToken);
    }
    StateRuntimeStatus BindPersistence(StatePersistenceBindingView<TState> binding) noexcept {
        std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
        if(_phase.load(std::memory_order_relaxed)!=Phase::Uninitialized) return StateRuntimeStatus::Frozen;
        if(_persistence || !binding) return StateRuntimeStatus::InvalidConfiguration;
        _persistence=binding;
        return StateRuntimeStatus::Success;
    }
    StateRuntimeStatus BindConvergence(StateConvergenceBindingView<TState> binding) noexcept {
        std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
        if(_phase.load(std::memory_order_relaxed)!=Phase::Uninitialized) return StateRuntimeStatus::Frozen;
        if(!binding) return StateRuntimeStatus::InvalidConfiguration;
        if(_convergence)
            return (_convergence.Owner==binding.Owner && _convergence.MarkLatestDirty==binding.MarkLatestDirty)
                ? StateRuntimeStatus::Success : StateRuntimeStatus::InvalidConfiguration;
        _convergence=binding;
        return StateRuntimeStatus::Success;
    }

    StateRuntimeStatus StageObserverTarget(StateObserverTargetNode& target) noexcept {
        std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
        const auto phase=_phase.load(std::memory_order_relaxed);
        if(phase==Phase::Running || phase==Phase::Stopping || phase==Phase::Stopped) return StateRuntimeStatus::Frozen;
        if(!target || target.Linked.load(std::memory_order_relaxed)) return StateRuntimeStatus::InvalidConfiguration;
        target.Next=_observerTargets;
        target.Linked.store(true,std::memory_order_release);
        _observerTargets=&target;
        return StateRuntimeStatus::Success;
    }
    void RemoveObserverTarget(StateObserverTargetNode& target) noexcept {
        std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
        auto** current=&_observerTargets;
        while(*current){
            if(*current==&target){
                *current=target.Next;
                target.Next=nullptr;
                target.Linked.store(false,std::memory_order_release);
                return;
            }
            current=&((*current)->Next);
        }
        target.Linked.store(false,std::memory_order_release);
        target.Next=nullptr;
    }
    bool ValidateStart() const noexcept {
        std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
        for(auto* target=_observerTargets;target;target=target->Next)
            if(!target->Linked.load(std::memory_order_relaxed) || !target->Validate || !target->Validate(target->Owner)) return false;
        return true;
    }

    StateRuntimeStatus Initialize(Timing::QualifiedTime(*captureTime)()=nullptr) noexcept {
        std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
        if(_phase.load(std::memory_order_relaxed)!=Phase::Uninitialized) return StateRuntimeStatus::AlreadyInitialized;
        if((TState::IsTransmissibleState || bool(_persistence)) && !System::RuntimeIdentity::IsInstalled()) return StateRuntimeStatus::IdentityUnavailable;
        if(_persistence){
            if(!_persistence.Validate(_persistence.Owner)) return StateRuntimeStatus::InvalidConfiguration;
            Value restored{};
            Timing::QualifiedTime restoredTruth{};
            bool restoredHasValue=false;
            const auto restoredStatus=_persistence.Restore(_persistence.Owner,restored,restoredTruth,restoredHasValue);
            if(restoredStatus!=StateRuntimeStatus::Success) return restoredStatus;
            if(restoredHasValue){
                Value prepared{};
                if(!_storage.Prepare(restored,prepared)) return StateRuntimeStatus::PersistenceFailure;
                _storage.CommitPrepared(prepared);
                _truthTime=restoredTruth;
                _version={false,1};
                _hasValue=true;
                _restoredDuringInitialize=true;
            }
        }
        _captureTime=captureTime?captureTime:&CaptureSystemTime;
        _phase.store(Phase::Prepared,std::memory_order_release);
        return StateRuntimeStatus::Success;
    }
    void StartValidated() noexcept {
        _restoredDuringInitialize=false;
        _phase.store(Phase::Running,std::memory_order_release);
    }
    StateRuntimeStatus RollbackInitialization() noexcept {
        std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
        if(_phase.load(std::memory_order_relaxed)==Phase::Prepared){
            _captureTime=nullptr;
            if(_restoredDuringInitialize){
                _hasValue=false;
                _version={};
                _truthTime={};
                _restoredDuringInitialize=false;
            }
            _phase.store(Phase::Uninitialized);
        }
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
    bool HasPersistence() const noexcept { std::lock_guard<System::Synchronization::Mutex> lock(_mutex);return bool(_persistence); }
    StateVersion Version() const noexcept { std::lock_guard<System::Synchronization::Mutex> lock(_mutex);return _version; }
    bool TryRead(StateSnapshot<TState>& output) const noexcept {
        std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
        if(!_hasValue) return false;
        _storage.CopyOut(output.Value);
        output.TruthTime=_truthTime;
        return true;
    }
};

template<class TState> void StateOwner<TState>::Release() noexcept {
    auto* runtime=std::exchange(_runtime,nullptr);
    const auto token=std::exchange(_token,0);
    if(runtime) runtime->ReleaseOwner(token);
}
template<class TState> StateSetStatus StateOwner<TState>::Set(const typename TState::ValueType& candidate){
    if(!_runtime || !_token) return StateSetStatus::OwnerUnavailable;
    const auto truth=_runtime->_captureTime ? _runtime->_captureTime() : Timing::QualifiedTime{};
    return _runtime->Set(_token,candidate,truth);
}
template<class TState> StateSetStatus StateOwner<TState>::Set(const typename TState::ValueType& candidate,Timing::QualifiedTime truthTime){
    return (!_runtime || !_token) ? StateSetStatus::OwnerUnavailable : _runtime->Set(_token,candidate,truthTime);
}
}
