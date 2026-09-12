#pragma once
#include <cstddef>
#include <cstdint>
#include "ESPressio_StateRemoteSession.hpp"

namespace ESPressio::State {

/// <summary>Local selector topology for one Transmissible State Type.</summary>
/// <remarks>AnyDevice is expanded by a capable adapter into concrete owner devices. It is never a State V1 wire kind.</remarks>
enum class StateSubscriptionSelectorMode : std::uint8_t {
    None,
    SpecificDevice,
    AnyDevice
};

/// <summary>Result of starting an adapter-expanded AnyDevice subscription.</summary>
struct StateAnySubscriptionResult final {
    StateRemoteStatus Status=StateRemoteStatus::TransportUnavailable;
    std::size_t SessionsStarted=0;
    constexpr explicit operator bool() const noexcept { return Status==StateRemoteStatus::Success; }
};

} // namespace ESPressio::State
