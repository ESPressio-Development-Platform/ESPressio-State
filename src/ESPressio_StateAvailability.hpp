#pragma once

#include <cstdint>

namespace ESPressio {
namespace State {

/// <summary>Authoritative/effective availability of one State value.</summary>
enum class StateAvailability : uint8_t {
    Available = 0,
    Stale,
    Unavailable,
    Expired
};

/// <summary>Explains why a State is not presently available as a current authoritative fact.</summary>
enum class StateAvailabilityReason : uint8_t {
    None = 0,
    SourceUnbound,
    SourceUnreachable
};

/// <summary>Transport-independent reachability of the device that owns a remote State.</summary>
enum class StateSourceReachability : uint8_t {
    Unknown = 0,
    Reachable,
    Stale,
    Unreachable
};

/// <summary>Availability plus its currently effective reason.</summary>
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
