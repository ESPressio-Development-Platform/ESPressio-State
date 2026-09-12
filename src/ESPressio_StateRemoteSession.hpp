#pragma once
#include <cstdint>
#include <mutex>
#include <ESPressio_DeviceRuntimeIdentity.hpp>
#include <ESPressio_Synchronization.hpp>
#include "ESPressio_StateTypes.hpp"

namespace ESPressio::State {

enum class StateRemoteSessionState : std::uint8_t {
    Inactive,
    Establishing,
    ActiveNoBaseline,
    ActiveTrusted,
    ResyncRequired,
    AwaitingResync,
    Closing
};

enum class StateRemoteStatus : std::uint8_t {
    Success,
    NotRunning,
    CapacityUnavailable,
    NotFound,
    InvalidIdentity,
    InvalidSession,
    SessionMismatch,
    ProvenanceMismatch,
    Duplicate,
    Older,
    AwaitingResync,
    ResyncRequired,
    TokenExhausted,
    TransportUnavailable,
    Conflict,
    Busy
};

enum class StateReplicaRelease : std::uint8_t { RetainLastKnown, ReleaseReplica };

/// <summary>Monotonic non-zero token sequence scoped to one requester RuntimeIncarnation.</summary>
/// <remarks>Values are never reused or wrapped. The optional high-water constructor is useful for
/// deterministic exhaustion validation and for composition that restores a stricter local high-water.</remarks>
class StateSessionTokenGenerator final {
    std::uint32_t _highWater=0;
public:
    constexpr StateSessionTokenGenerator() noexcept=default;
    constexpr explicit StateSessionTokenGenerator(std::uint32_t highWater) noexcept:_highWater(highWater){}
    bool TryAllocate(StateSessionToken& output) noexcept {
        if(_highWater==UINT32_MAX) return false;
        output=StateSessionToken{++_highWater};return true;
    }
    constexpr std::uint32_t HighWater() const noexcept { return _highWater; }
};

class StateResyncTokenGenerator final {
    std::uint32_t _highWater=0;
public:
    constexpr StateResyncTokenGenerator() noexcept=default;
    constexpr explicit StateResyncTokenGenerator(std::uint32_t highWater) noexcept:_highWater(highWater){}
    bool TryAllocate(StateResyncToken& output) noexcept {
        if(_highWater==UINT32_MAX) return false;
        output=StateResyncToken{++_highWater};return true;
    }
    constexpr std::uint32_t HighWater() const noexcept { return _highWater; }
};

namespace Detail {
/// <summary>Blocking local access or one nonblocking family-ingress lock attempt.</summary>
template<bool NonBlocking>
class StateAdmissionLock final : public std::unique_lock<System::Synchronization::Mutex> {
public:
    explicit StateAdmissionLock(System::Synchronization::Mutex& mutex) noexcept
        : std::unique_lock<System::Synchronization::Mutex>(mutex,std::defer_lock) {
        if constexpr(NonBlocking) (void)this->try_lock();
        else this->lock();
    }
};
/// <summary>One non-resettable token authority shared by every State Runtime in this process.</summary>
/// <remarks>System identity cannot change in a process. Destroying/reconstructing a family Runtime,
/// or configuring disjoint Type packs, therefore must not restart either token namespace. The
/// mutex is resolved during initialization; allocation neither grows storage nor wraps on exhaustion.</remarks>
class StateProcessTokenAuthority final {
    inline static System::Synchronization::Mutex _mutex;
    inline static StateSessionTokenGenerator _sessions{};
    inline static StateResyncTokenGenerator _resyncs{};
    StateProcessTokenAuthority()=delete;
public:
    static void Initialize() noexcept { std::lock_guard<System::Synchronization::Mutex> lock(_mutex); }
    static bool TryAllocate(StateSessionToken& output) noexcept {
        std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
        return _sessions.TryAllocate(output);
    }
    static StateRemoteStatus TryAllocateResyncNonBlocking(StateResyncToken& output) noexcept {
        std::unique_lock<System::Synchronization::Mutex> lock(_mutex,std::try_to_lock);
        if(!lock) return StateRemoteStatus::Busy;
        return _resyncs.TryAllocate(output)?StateRemoteStatus::Success:StateRemoteStatus::TokenExhausted;
    }
    static bool TryAllocate(StateResyncToken& output) noexcept {
        std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
        return _resyncs.TryAllocate(output);
    }
};
}

struct StateSubscriptionHandle final {
    StateTypeId TypeId{};
    System::DeviceIdentifier OwnerDevice{};
    StateSessionToken Session{};
    constexpr explicit operator bool() const noexcept { return bool(TypeId) && bool(OwnerDevice) && bool(Session); }
};

struct StateSubscriptionResult final {
    StateRemoteStatus Status=StateRemoteStatus::TransportUnavailable;
    StateSubscriptionHandle Handle{};
    constexpr explicit operator bool() const noexcept { return Status==StateRemoteStatus::Success; }
};

} // namespace ESPressio::State
