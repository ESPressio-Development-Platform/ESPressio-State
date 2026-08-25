#pragma once

#include <array>
#include <cstddef>
#include <mutex>

#include "ESPressio_DeviceIdentifier.hpp"
#include "ESPressio_StateContract.hpp"

namespace ESPressio {
namespace State {

template<typename TContract, std::size_t TMaximumSubscribers>
class StateSubscriberRegistry final {
    struct SubscriberRecord {
        bool Used = false;
        DeviceIdentifier Device{};
        std::array<bool, TContract::StateCount> States{};
    };

    std::array<SubscriberRecord, TMaximumSubscribers> _subscribers{};
    mutable std::mutex _mutex;

    SubscriberRecord* FindLocked(const DeviceIdentifier& device) {
        for (auto& subscriber : _subscribers) {
            if (subscriber.Used && subscriber.Device == device) return &subscriber;
        }
        return nullptr;
    }

    SubscriberRecord* FindOrCreateLocked(const DeviceIdentifier& device) {
        if (device.IsZero()) return nullptr;
        if (auto* existing = FindLocked(device)) return existing;
        for (auto& subscriber : _subscribers) {
            if (!subscriber.Used) {
                subscriber.Used = true;
                subscriber.Device = device;
                return &subscriber;
            }
        }
        return nullptr;
    }

public:
    static constexpr std::size_t MaximumSubscribers = TMaximumSubscribers;

    bool Subscribe(const DeviceIdentifier& device, StateTypeId typeId) {
        std::size_t index = 0;
        if (!TContract::TryIndexOf(typeId, index)) return false;
        std::lock_guard<std::mutex> lock(_mutex);
        auto* subscriber = FindOrCreateLocked(device);
        if (subscriber == nullptr) return false;
        subscriber->States[index] = true;
        return true;
    }

    bool Unsubscribe(const DeviceIdentifier& device, StateTypeId typeId) {
        std::size_t index = 0;
        if (!TContract::TryIndexOf(typeId, index)) return false;
        std::lock_guard<std::mutex> lock(_mutex);
        auto* subscriber = FindLocked(device);
        if (subscriber == nullptr) return false;
        subscriber->States[index] = false;
        return true;
    }

    bool IsSubscribed(const DeviceIdentifier& device, StateTypeId typeId) const {
        std::size_t index = 0;
        if (!TContract::TryIndexOf(typeId, index)) return false;
        std::lock_guard<std::mutex> lock(_mutex);
        for (const auto& subscriber : _subscribers) {
            if (subscriber.Used && subscriber.Device == device) {
                return subscriber.States[index];
            }
        }
        return false;
    }

    template<typename TDefinition>
    bool IsSubscribed(const DeviceIdentifier& device) const {
        return IsSubscribed(device, StateTypeIdOf<TDefinition>);
    }

    bool Remove(const DeviceIdentifier& device) {
        std::lock_guard<std::mutex> lock(_mutex);
        auto* subscriber = FindLocked(device);
        if (subscriber == nullptr) return false;
        *subscriber = SubscriberRecord{};
        return true;
    }

    template<typename TCallback>
    void ForEachSubscriber(StateTypeId typeId, TCallback&& callback) const {
        std::size_t index = 0;
        if (!TContract::TryIndexOf(typeId, index)) return;
        std::array<DeviceIdentifier, TMaximumSubscribers> matches{};
        std::size_t count = 0;
        {
            std::lock_guard<std::mutex> lock(_mutex);
            for (const auto& subscriber : _subscribers) {
                if (subscriber.Used && subscriber.States[index]) {
                    matches[count++] = subscriber.Device;
                }
            }
        }
        for (std::size_t i = 0; i < count; ++i) callback(matches[i]);
    }

    template<typename TCallback>
    void ForEachSubscribedType(
        const DeviceIdentifier& device,
        TCallback&& callback
    ) const {
        std::array<StateTypeId, TContract::StateCount> types{};
        std::size_t count = 0;
        {
            std::lock_guard<std::mutex> lock(_mutex);
            for (const auto& subscriber : _subscribers) {
                if (!subscriber.Used || subscriber.Device != device) continue;
                for (std::size_t index = 0; index < TContract::StateCount; ++index) {
                    if (subscriber.States[index]) types[count++] = TContract::TypeIds[index];
                }
                break;
            }
        }
        for (std::size_t index = 0; index < count; ++index) callback(types[index]);
    }
};

}
}
