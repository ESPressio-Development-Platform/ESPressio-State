#pragma once

#include <array>
#include <cstddef>
#include <tuple>
#include <type_traits>

#include "ESPressio_DeviceIdentifier.hpp"
#include "ESPressio_StateContract.hpp"

namespace ESPressio {
namespace State {

enum class RemoteDeviceAvailability : uint8_t {
    Unknown = 0,
    Connected,
    Stale,
    Disconnected,
    ConnectionLost
};

template<typename TValue>
struct RemoteStateSlot {
    TValue Value{};
    StateEpoch Epoch = 0;
    StateRevision Revision = 0;
    bool HasValue = false;
};

template<typename TContract>
struct RemoteStateTuple;

template<typename... TDefinitions>
struct RemoteStateTuple<StateContract<TDefinitions...>> {
    using Type = std::tuple<RemoteStateSlot<StateValueType<TDefinitions>>...>;
};

template<typename TContract, std::size_t TMaximumDevices>
class RemoteStateManager final {
public:
    using Contract = TContract;
    static constexpr std::size_t MaximumDevices = TMaximumDevices;

private:
    struct DeviceRecord {
        bool Used = false;
        DeviceIdentifier Identifier{};
        RemoteDeviceAvailability Availability = RemoteDeviceAvailability::Unknown;
        typename RemoteStateTuple<TContract>::Type States{};
    };

    std::array<DeviceRecord, TMaximumDevices> _devices{};

    DeviceRecord* Find(const DeviceIdentifier& identifier) {
        for (auto& device : _devices) {
            if (device.Used && device.Identifier == identifier) return &device;
        }
        return nullptr;
    }

    const DeviceRecord* Find(const DeviceIdentifier& identifier) const {
        for (const auto& device : _devices) {
            if (device.Used && device.Identifier == identifier) return &device;
        }
        return nullptr;
    }

    DeviceRecord* FindOrCreate(const DeviceIdentifier& identifier) {
        if (identifier.IsZero()) return nullptr;
        if (auto* existing = Find(identifier)) return existing;
        for (auto& device : _devices) {
            if (!device.Used) {
                device.Used = true;
                device.Identifier = identifier;
                return &device;
            }
        }
        return nullptr;
    }

public:
    template<typename TDefinition>
    const RemoteStateSlot<StateValueType<TDefinition>>* Get(const DeviceIdentifier& identifier) const {
        static_assert(TContract::template Contains<TDefinition>, "State definition is not part of this StateContract");
        const auto* device = Find(identifier);
        if (device == nullptr) return nullptr;
        return &std::get<TContract::template IndexOf<TDefinition>()>(device->States);
    }

    template<typename TDefinition>
    RemoteStateSlot<StateValueType<TDefinition>>* Get(const DeviceIdentifier& identifier) {
        static_assert(TContract::template Contains<TDefinition>, "State definition is not part of this StateContract");
        auto* device = Find(identifier);
        if (device == nullptr) return nullptr;
        return &std::get<TContract::template IndexOf<TDefinition>()>(device->States);
    }

    template<typename TDefinition>
    bool Apply(
        const DeviceIdentifier& identifier,
        StateEpoch epoch,
        StateRevision revision,
        const StateValueType<TDefinition>& value
    ) {
        static_assert(TContract::template Contains<TDefinition>, "State definition is not part of this StateContract");
        auto* device = FindOrCreate(identifier);
        if (device == nullptr || revision == 0) return false;

        auto& slot = std::get<TContract::template IndexOf<TDefinition>()>(device->States);
        if (slot.HasValue) {
            if (epoch < slot.Epoch) return false;
            if (epoch == slot.Epoch && revision <= slot.Revision) return false;
        }

        slot.Value = value;
        slot.Epoch = epoch;
        slot.Revision = revision;
        slot.HasValue = true;
        return true;
    }

    bool SetAvailability(const DeviceIdentifier& identifier, RemoteDeviceAvailability availability) {
        auto* device = FindOrCreate(identifier);
        if (device == nullptr) return false;
        device->Availability = availability;
        return true;
    }

    RemoteDeviceAvailability GetAvailability(const DeviceIdentifier& identifier) const {
        const auto* device = Find(identifier);
        return device != nullptr ? device->Availability : RemoteDeviceAvailability::Unknown;
    }

    std::size_t GetDeviceCount() const {
        std::size_t count = 0;
        for (const auto& device : _devices) if (device.Used) ++count;
        return count;
    }
};

}
}
