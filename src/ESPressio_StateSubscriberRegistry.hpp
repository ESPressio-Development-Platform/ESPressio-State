#pragma once

#include <array>
#include <cstddef>
#include <memory>
#include <mutex>

#include <ESPressio_ThreadSafeObservable.hpp>

#include "ESPressio_DeviceIdentifier.hpp"
#include "ESPressio_StateContract.hpp"
#include "ESPressio_StateObservers.hpp"

namespace ESPressio {
namespace State {

template<typename TContract, std::size_t TMaximumSubscribers>
class StateSubscriberRegistry final {
    class RegistryObservable final : public Observable::ThreadSafeObservable {
    public:
        void Added(const DeviceIdentifier& device, StateTypeId typeId) {
            ExecuteNotification([&](NotificationContext& notification) {
                notification.WithObservers<IStateSubscriberRegistryObserver>(
                    [&](IStateSubscriberRegistryObserver* observer) {
                        observer->OnRemoteStateSubscriberAdded(device, typeId);
                    }
                );
            });
        }

        void Removed(const DeviceIdentifier& device, StateTypeId typeId) {
            ExecuteNotification([&](NotificationContext& notification) {
                notification.WithObservers<IStateSubscriberRegistryObserver>(
                    [&](IStateSubscriberRegistryObserver* observer) {
                        observer->OnRemoteStateSubscriberRemoved(device, typeId);
                    }
                );
            });
        }

        void DeviceRemoved(const DeviceIdentifier& device) {
            ExecuteNotification([&](NotificationContext& notification) {
                notification.WithObservers<IStateSubscriberRegistryObserver>(
                    [&](IStateSubscriberRegistryObserver* observer) {
                        observer->OnRemoteStateSubscriberDeviceRemoved(device);
                    }
                );
            });
        }

        void CapacityExhausted(const DeviceIdentifier& device, StateTypeId typeId) {
            ExecuteNotification([&](NotificationContext& notification) {
                notification.WithObservers<IStateSubscriberRegistryObserver>(
                    [&](IStateSubscriberRegistryObserver* observer) {
                        observer->OnRemoteStateSubscriberCapacityExhausted(device, typeId);
                    }
                );
            });
        }
    };

    struct SubscriberRecord {
        bool Used = false;
        DeviceIdentifier Device{};
        std::array<bool, TContract::StateCount> States{};
    };

    std::array<SubscriberRecord, TMaximumSubscribers> _subscribers{};
    mutable std::mutex _mutex;
    std::shared_ptr<RegistryObservable> _observable =
        std::make_shared<RegistryObservable>();

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

    Observable::ObserverHandlePtr RegisterObserver(
        IStateSubscriberRegistryObserver* observer
    ) {
        return _observable->RegisterObserver(observer);
    }

    void UnregisterObserver(IStateSubscriberRegistryObserver* observer) {
        _observable->UnregisterObserver(observer);
    }

    bool Subscribe(const DeviceIdentifier& device, StateTypeId typeId) {
        std::size_t index = 0;
        if (!TContract::TryIndexOf(typeId, index)) return false;

        bool added = false;
        bool capacityExhausted = false;
        {
            std::lock_guard<std::mutex> lock(_mutex);
            auto* subscriber = FindOrCreateLocked(device);
            if (subscriber == nullptr) {
                capacityExhausted = true;
            } else if (!subscriber->States[index]) {
                subscriber->States[index] = true;
                added = true;
            }
        }

        if (capacityExhausted) {
            _observable->CapacityExhausted(device, typeId);
            return false;
        }
        if (added) _observable->Added(device, typeId);
        return true;
    }

    bool Unsubscribe(const DeviceIdentifier& device, StateTypeId typeId) {
        std::size_t index = 0;
        if (!TContract::TryIndexOf(typeId, index)) return false;

        bool removed = false;
        {
            std::lock_guard<std::mutex> lock(_mutex);
            auto* subscriber = FindLocked(device);
            if (subscriber == nullptr || !subscriber->States[index]) return false;
            subscriber->States[index] = false;
            removed = true;
        }
        if (removed) _observable->Removed(device, typeId);
        return removed;
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

    bool HasSubscribers(StateTypeId typeId) const {
        std::size_t index = 0;
        if (!TContract::TryIndexOf(typeId, index)) return false;
        std::lock_guard<std::mutex> lock(_mutex);
        for (const auto& subscriber : _subscribers) {
            if (subscriber.Used && subscriber.States[index]) return true;
        }
        return false;
    }

    template<typename TDefinition>
    bool HasSubscribers() const {
        return HasSubscribers(StateTypeIdOf<TDefinition>);
    }

    bool Remove(const DeviceIdentifier& device) {
        bool removed = false;
        {
            std::lock_guard<std::mutex> lock(_mutex);
            auto* subscriber = FindLocked(device);
            if (subscriber == nullptr) return false;
            *subscriber = SubscriberRecord{};
            removed = true;
        }
        if (removed) _observable->DeviceRemoved(device);
        return removed;
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
                    if (subscriber.States[index]) {
                        types[count++] = TContract::TypeIds[index];
                    }
                }
                break;
            }
        }
        for (std::size_t index = 0; index < count; ++index) callback(types[index]);
    }
};

}
}
