#pragma once
#include <cstdint>
#include <ESPressio_DeviceRuntimeIdentity.hpp>
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
    Conflict
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
