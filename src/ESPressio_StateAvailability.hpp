#pragma once

#include <cstdint>

namespace ESPressio {
namespace State {

/// <summary>Authoritative/effective availability of one State value.</summary>
/**
 * ESPressio Memory Audit
 * Underlying storage: 1 bytes
 * Total Memory: 1 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
enum class StateAvailability : uint8_t {
    Available = 0,
    Stale,
    Unavailable,
    Expired
};

/// <summary>Explains why a State is not presently available as a current authoritative fact.</summary>
/**
 * ESPressio Memory Audit
 * Underlying storage: 1 bytes
 * Total Memory: 1 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
enum class StateAvailabilityReason : uint8_t {
    None = 0,
    SourceUnbound,
    SourceUnreachable
};

/// <summary>Transport-independent reachability of the device that owns a remote State.</summary>
/**
 * ESPressio Memory Audit
 * Underlying storage: 1 bytes
 * Total Memory: 1 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
enum class StateSourceReachability : uint8_t {
    Unknown = 0,
    Reachable,
    Stale,
    Unreachable
};

/// <summary>Availability plus its currently effective reason.</summary>
/**
 * ESPressio Memory Audit
 * Members:
 * - Availability (StateAvailability): 1 bytes [0 bytes dynamic allocation]
 * - Reason (StateAvailabilityReason): 1 bytes [0 bytes dynamic allocation]
 * Total Memory: 2 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
struct StateAvailabilityStatus final {
    StateAvailability Availability = StateAvailability::Unavailable;
    StateAvailabilityReason Reason = StateAvailabilityReason::SourceUnreachable;

    friend bool operator==(const StateAvailabilityStatus& left, const StateAvailabilityStatus& right) noexcept {
        return left.Availability == right.Availability && left.Reason == right.Reason;
    }
    friend bool operator!=(const StateAvailabilityStatus& left, const StateAvailabilityStatus& right) noexcept {
        return !(left == right);
    }
};

/// <summary>Combines source-authoritative availability with current source reachability.</summary>
inline StateAvailabilityStatus ResolveEffectiveStateAvailability(
    StateAvailability authoritativeAvailability,
    StateAvailabilityReason authoritativeReason,
    StateSourceReachability reachability
) noexcept {
    if (authoritativeAvailability == StateAvailability::Expired) {
        return {StateAvailability::Expired, authoritativeReason};
    }
    if (authoritativeAvailability == StateAvailability::Unavailable) {
        return {
            StateAvailability::Unavailable,
            authoritativeReason == StateAvailabilityReason::None
                ? StateAvailabilityReason::SourceUnbound
                : authoritativeReason
        };
    }
    if (reachability == StateSourceReachability::Unreachable) {
        return {StateAvailability::Unavailable, StateAvailabilityReason::SourceUnreachable};
    }
    if (authoritativeAvailability == StateAvailability::Stale ||
        reachability == StateSourceReachability::Stale ||
        reachability == StateSourceReachability::Unknown) {
        return {StateAvailability::Stale, StateAvailabilityReason::None};
    }
    return {StateAvailability::Available, StateAvailabilityReason::None};
}

} // namespace State
} // namespace ESPressio
