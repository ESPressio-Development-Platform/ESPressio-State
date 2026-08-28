#pragma once

#include <array>
#include <cstddef>
#include <memory>
#include <mutex>

#include <ESPressio_Memory.hpp>
#include <ESPressio_ThreadSafeObservable.hpp>

#include "ESPressio_DeviceIdentifier.hpp"
#include "ESPressio_StateContract.hpp"
#include "ESPressio_StateObservers.hpp"

namespace ESPressio {
namespace State {

/// <summary>Tracks remote devices subscribed to individual State definitions in a contract.</summary>
/// <typeparam name="TContract">State contract whose type IDs define the subscription bitset.</typeparam>
/// <typeparam name="TMaximumSubscribers">Maximum number of remote devices retained by the registry.</typeparam>
/// <remarks>Subscription records are bounded and stored in external-preferred memory. Observer callbacks are emitted after releasing the registry mutex.</remarks>
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

    using ExternalMemory = System::Memory::MemoryPolicy;
    System::Memory::Vector<SubscriberRecord, ExternalMemory::ExternalPreferred>
        _subscribers = System::Memory::Vector<SubscriberRecord, ExternalMemory::ExternalPreferred>(
            TMaximumSubscribers
        );
    mutable std::mutex _mutex;
    std::shared_ptr<RegistryObservable> _observable =
        System::Memory::MakeShared<
            RegistryObservable,
            ExternalMemory::ExternalPreferred
        >();

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
    /// <summary>Maximum number of remote subscriber devices retained simultaneously.</summary>
    static constexpr std::size_t MaximumSubscribers = TMaximumSubscribers;

    /// <summary>Registers an observer for remote-subscriber registry changes.</summary>
    /// <param name="observer">Observer to register.</param>
    /// <returns>An RAII observer handle whose destruction unregisters the observer.</returns>
    Observable::ObserverHandlePtr RegisterObserver(
        IStateSubscriberRegistryObserver* observer
    ) {
        return _observable->template RegisterObserverAs<
            IStateSubscriberRegistryObserver
        >(observer);
    }

    /// <summary>Explicitly unregisters a previously registered subscriber-registry observer.</summary>
    void UnregisterObserver(IStateSubscriberRegistryObserver* observer) {
        _observable->UnregisterObserver(observer);
    }

    /// <summary>Adds a remote device subscription for the supplied State type.</summary>
    /// <param name="device">Remote subscriber identity.</param>
    /// <param name="typeId">State type identifier from <typeparamref name="TContract"/>.</param>
    /// <returns><c>false</c> when the type is not part of the contract or subscriber capacity is exhausted; otherwise <c>true</c>, including an already-active subscription.</returns>
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

    /// <summary>Removes a remote device subscription for the supplied State type.</summary>
    /// <returns><c>true</c> only when an active subscription was removed.</returns>
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

    /// <summary>Tests whether a remote device is subscribed to a State type.</summary>
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

    /// <summary>Tests whether a remote device is subscribed to a typed State definition.</summary>
    template<typename TDefinition>
    bool IsSubscribed(const DeviceIdentifier& device) const {
        return IsSubscribed(device, StateTypeIdOf<TDefinition>);
    }

    /// <summary>Tests whether at least one remote device subscribes to the supplied State type.</summary>
    bool HasSubscribers(StateTypeId typeId) const {
        std::size_t index = 0;
        if (!TContract::TryIndexOf(typeId, index)) return false;
        std::lock_guard<std::mutex> lock(_mutex);
        for (const auto& subscriber : _subscribers) {
            if (subscriber.Used && subscriber.States[index]) return true;
        }
        return false;
    }

    /// <summary>Tests whether at least one remote device subscribes to a typed State definition.</summary>
    template<typename TDefinition>
    bool HasSubscribers() const {
        return HasSubscribers(StateTypeIdOf<TDefinition>);
    }

    /// <summary>Removes all subscription state for a remote device.</summary>
    /// <returns><c>true</c> when a subscriber record was present and removed.</returns>
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

    /// <summary>Invokes a callback once for each remote device subscribed to the supplied State type.</summary>
    /// <typeparam name="TCallback">Callable accepting <c>const DeviceIdentifier&amp;</c>.</typeparam>
    /// <remarks>A snapshot is built while locked and callbacks execute after releasing the registry mutex, allowing safe re-entry into registry operations.</remarks>
    template<typename TCallback>
    void ForEachSubscriber(StateTypeId typeId, TCallback&& callback) const {
        std::size_t index = 0;
        if (!TContract::TryIndexOf(typeId, index)) return;
        System::Memory::Vector<
            DeviceIdentifier,
            ExternalMemory::ExternalPreferred
        > matches;
        matches.reserve(TMaximumSubscribers);
        {
            std::lock_guard<std::mutex> lock(_mutex);
            for (const auto& subscriber : _subscribers) {
                if (subscriber.Used && subscriber.States[index]) {
                    matches.push_back(subscriber.Device);
                }
            }
        }
        for (const auto& device : matches) callback(device);
    }

    /// <summary>Invokes a callback once for each State type to which a remote device is subscribed.</summary>
    /// <typeparam name="TCallback">Callable accepting a <c>StateTypeId</c>.</typeparam>
    /// <remarks>Type IDs are snapshotted before callbacks execute so callbacks may safely mutate subscriptions.</remarks>
    template<typename TCallback>
    void ForEachSubscribedType(
        const DeviceIdentifier& device,
        TCallback&& callback
    ) const {
        System::Memory::Vector<
            StateTypeId,
            ExternalMemory::ExternalPreferred
        > types;
        types.reserve(TContract::StateCount);
        {
            std::lock_guard<std::mutex> lock(_mutex);
            for (const auto& subscriber : _subscribers) {
                if (!subscriber.Used || subscriber.Device != device) continue;
                for (std::size_t index = 0; index < TContract::StateCount; ++index) {
                    if (subscriber.States[index]) {
                        types.push_back(TContract::TypeIds[index]);
                    }
                }
                break;
            }
        }
        for (StateTypeId type : types) callback(type);
    }
};

} // namespace State
} // namespace ESPressio
