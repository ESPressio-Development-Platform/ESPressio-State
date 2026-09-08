#pragma once

#include "ESPressio_DeviceIdentifier.hpp"
#include "ESPressio_StateContract.hpp"

namespace ESPressio {
namespace State {

/// <summary>Uniquely identifies one authoritative State within the ESPressio platform.</summary>
/// <remarks>
/// A StateAddress is deliberately transport-independent. Device identifies the
/// permanent authoritative device and TypeId identifies the semantic State
/// definition. There is no separate source identifier or transport address.
/// </remarks>
/**
 * ESPressio Memory Audit
 * Members:
 * - Device (DeviceIdentifier): 16 bytes [0 bytes dynamic allocation]
 * - TypeId (StateTypeId): 8 bytes [0 bytes dynamic allocation]
 * Total Memory: 24 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
struct StateAddress final {
    DeviceIdentifier Device{};
    StateTypeId TypeId = 0;

    /// <summary>Indicates whether both address components are valid.</summary>
    explicit operator bool() const noexcept {
        return static_cast<bool>(Device) && TypeId != 0;
    }

    friend bool operator==(const StateAddress& left, const StateAddress& right) noexcept {
        return left.Device == right.Device && left.TypeId == right.TypeId;
    }

    friend bool operator!=(const StateAddress& left, const StateAddress& right) noexcept {
        return !(left == right);
    }

    friend bool operator<(const StateAddress& left, const StateAddress& right) noexcept {
        if (left.Device < right.Device) return true;
        if (right.Device < left.Device) return false;
        return left.TypeId < right.TypeId;
    }
};

/// <summary>Creates the canonical address for a typed State definition on a device.</summary>
template<typename TDefinition>
StateAddress MakeStateAddress(const DeviceIdentifier& device) noexcept {
    return StateAddress{device, StateTypeIdOf<TDefinition>};
}

} // namespace State
} // namespace ESPressio
