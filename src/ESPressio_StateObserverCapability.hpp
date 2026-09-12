#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <ESPressio_ThreadCapability.hpp>
#include "ESPressio_StateObserverTarget.hpp"
#include "ESPressio_StateTypeRuntime.hpp"

namespace ESPressio::State {

struct ObserverCapabilityTag final {};

namespace Detail {
template<class... T> struct UniqueStateTypes : std::true_type {};
template<class First,class... Rest>
struct UniqueStateTypes<First,Rest...> :
    std::bool_constant<(!std::is_same_v<First,Rest> && ...) && UniqueStateTypes<Rest...>::value> {};

struct StateChangeMemberAnchor {};
}

/// <summary>Identity-only view of one captured State observer pending bitmap.</summary>
/// <remarks>The view is valid only for the duration of the application callback. It contains no
/// State values; callers explicitly TryRead the current StateSnapshot after inspecting the identities.</remarks>
class StateChangeSet final {
    const StateTypeId* _types=nullptr;
    const std::uint64_t* _words=nullptr;
    std::size_t _count=0;
public:
    constexpr StateChangeSet() noexcept=default;
    constexpr StateChangeSet(const StateTypeId* types,const std::uint64_t* words,std::size_t count) noexcept
        :_types(types),_words(words),_count(count){}
    std::size_t TypeCount() const noexcept { return _count; }
    bool Contains(StateTypeId type) const noexcept {
        if(!_types || !_words) return false;
        for(std::size_t i=0;i<_count;++i)
            if(_types[i]==type) return (_words[i/64] & (std::uint64_t{1}<<(i%64)))!=0;
        return false;
    }
    template<class TState> bool Contains() const noexcept { return Contains(TState::TypeId); }
};

/// <summary>TH10 latest-truth observation capability over a fixed State Type set.</summary>
/// <remarks>Producer commits publish only one atomic bit and the owning Thread's common Wake.
/// No State value is queued or copied. Service captures and clears pending identities before
/// application code, so changes during the callback remain live for a later quantum.</remarks>
template<class... TStates>
class ObserverCapability final : public Threads::ThreadCapability {
    static_assert(sizeof...(TStates)>0,"State observer capability requires at least one Type");
    static_assert(Detail::UniqueStateTypes<TStates...>::value,"State observer capability Types must be unique");
    static_assert((Detail::ValidateLocalStateType<TStates>() && ...));
    static constexpr std::size_t TypeCountValue=sizeof...(TStates);
    static constexpr std::size_t WordCount=(TypeCountValue+63)/64;
    inline static constexpr std::array<StateTypeId,TypeCountValue> TypeIds{TStates::TypeId...};

    std::array<StateObserverTargetNode,TypeCountValue> _targets{};
    std::array<std::atomic<std::uint64_t>,WordCount> _pendingWords{};
    std::atomic<bool> _pending{false};
    Threads::ThreadHostServices _host{};
    void* _callbackOwner=nullptr;
    void (Detail::StateChangeMemberAnchor::*_callback)(const StateChangeSet&)=nullptr;
    void (*_invoke)(void*,void (Detail::StateChangeMemberAnchor::*)(const StateChangeSet&),const StateChangeSet&)=nullptr;
    bool _bound=false;
    bool _frozen=false;
    bool _bindingError=false;
    bool _activatedOnce=false;

    template<class T> static constexpr std::size_t Index() noexcept {
        static_assert((std::is_same_v<T,TStates> || ...),"State observation requires a declared Type");
        std::size_t i=0,result=0;
        ((std::is_same_v<T,TStates> ? (void)(result=i) : (void)0,++i),...);
        return result;
    }
    void Publish(std::size_t index) noexcept {
        _pendingWords[index/64].fetch_or(std::uint64_t{1}<<(index%64),std::memory_order_release);
        if(!_pending.exchange(true,std::memory_order_acq_rel)) (void)_host.Wake();
    }
    template<class T> bool Stage() noexcept {
        auto& target=_targets[Index<T>()];
        target.Owner=this;
        target.ObservationIndex=Index<T>();
        target.Validate=[](const void* p) noexcept {
            const auto& self=*static_cast<const ObserverCapability*>(p);
            return self._frozen && self._callbackOwner && self._invoke;
        };
        target.Publish=[](void* p,std::size_t index) noexcept {
            static_cast<ObserverCapability*>(p)->Publish(index);
        };
        return StateTypeRuntime<T>::Get().StageObserverTarget(target)==StateRuntimeStatus::Success;
    }
    void DetachTargets() noexcept {
        (StateTypeRuntime<TStates>::Get().RemoveObserverTarget(_targets[Index<TStates>()]),...);
    }
    void ClearPending() noexcept {
        _pending.store(false,std::memory_order_release);
        for(auto& word:_pendingWords) word.store(0,std::memory_order_release);
    }
public:
    using CapabilityTag=ObserverCapabilityTag;
    static constexpr std::uint32_t FrameworkStackFloorBytes=512;
    static constexpr std::size_t ExternalStorageBytes=0;
    static constexpr std::size_t ExternalStorageAlignment=1;
    static constexpr bool NeedsMonotonicTime=false;
    static constexpr std::size_t ObservationCount=TypeCountValue;
    static constexpr std::size_t PendingBitmapBytes=WordCount*sizeof(std::uint64_t);

    ObserverCapability() noexcept=default;
    ObserverCapability(const ObserverCapability&)=delete;
    ObserverCapability& operator=(const ObserverCapability&)=delete;
    ~ObserverCapability(){ if(_bound) DetachTargets(); }

    template<class Owner>
    bool OnChange(Owner& owner,void (Owner::*member)(const StateChangeSet&)) noexcept {
        if(_bound || _frozen || _invoke || member==nullptr){_bindingError=true;return false;}
        _callbackOwner=&owner;
        _callback=reinterpret_cast<void (Detail::StateChangeMemberAnchor::*)(const StateChangeSet&)>(member);
        _invoke=[](void* value,void (Detail::StateChangeMemberAnchor::*stored)(const StateChangeSet&),const StateChangeSet& changes){
            const auto method=reinterpret_cast<void (Owner::*)(const StateChangeSet&)>(stored);
            (static_cast<Owner*>(value)->*method)(changes);
        };
        return true;
    }

    Threads::ThreadStatus Initialize(const Threads::ThreadHostServices& host) noexcept {
        if(_bindingError || !_callbackOwner || !_invoke || !host.Owner || !host.WakeFunction || !host.AcceptingFunction)
            return Threads::ThreadStatus::InitializationFailed;
        _host=host;
        _activatedOnce=false;
        ClearPending();
        bool valid=true;
        ((valid ? (void)(valid=Stage<TStates>()) : (void)0),...);
        if(!valid){DetachTargets();return Threads::ThreadStatus::InitializationFailed;}
        _bound=true;
        return Threads::ThreadStatus::Success;
    }
    Threads::ThreadStatus FinalizeInitialization() noexcept {
        if(!_bound || _bindingError || !_callbackOwner || !_invoke) return Threads::ThreadStatus::InitializationFailed;
        _frozen=true;
        return Threads::ThreadStatus::Success;
    }
    void RollbackInitialization() noexcept {
        if(_bound) DetachTargets();
        _bound=false;_frozen=false;_activatedOnce=false;ClearPending();
    }
    void Activate(const Threads::ThreadCycleContext&) noexcept {
        if(!_bound || !_frozen || _activatedOnce) return;
        _activatedOnce=true;
        // Only first activation synthesizes the "already has value" identity. Pause/resume
        // preserves existing pending bits and must not manufacture repeated callbacks.
        ((StateTypeRuntime<TStates>::Get().HasValue() ? Publish(Index<TStates>()) : (void)0),...);
    }
    void Pause(const Threads::ThreadCycleContext&) noexcept {}
    void Quiesce(const Threads::ThreadCycleContext&) noexcept {
        if(_bound) DetachTargets();
        _bound=false;_frozen=false;_activatedOnce=false;ClearPending();
    }
    Threads::CapabilityReadiness Readiness(const Threads::ThreadCycleContext&) const noexcept {
        Threads::CapabilityReadiness ready;
        ready.Immediate=_bound && _frozen && _pending.load(std::memory_order_acquire);
        return ready;
    }
    void Service(const Threads::ThreadCycleContext&) {
        if(!_bound || !_frozen) return;
        // Clearing the aggregate first is the race boundary: a producer after this store
        // necessarily republishes true. Word exchanges then capture one latest-truth quantum.
        _pending.store(false,std::memory_order_release);
        std::array<std::uint64_t,WordCount> captured{};
        bool any=false;
        for(std::size_t i=0;i<WordCount;++i){
            captured[i]=_pendingWords[i].exchange(0,std::memory_order_acq_rel);
            any=any || captured[i]!=0;
        }
        if(any){
            const StateChangeSet changes(TypeIds.data(),captured.data(),TypeCountValue);
            _invoke(_callbackOwner,_callback,changes);
        }
    }
    bool BeforeApplication(const Threads::ThreadCycleContext&) noexcept { return true; }
    void AfterApplication(const Threads::ThreadCycleContext&) noexcept {}

    bool HasPending() const noexcept { return _pending.load(std::memory_order_acquire); }
};

} // namespace ESPressio::State
