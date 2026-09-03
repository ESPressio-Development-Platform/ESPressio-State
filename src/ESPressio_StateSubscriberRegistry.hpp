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
/// <typeparam name="TMaximumObservers">Maximum simultaneous lifecycle observer registrations.</typeparam>
/// <remarks>Construction is allocation-free. Subscription records are bounded and materialized in external-preferred memory on first use. Observer callbacks are emitted after releasing the registry mutex, observer infrastructure is only allocated when required, and observer registration is independently bounded.</remarks>
template<typename TContract, std::size_t TMaximumSubscribers, std::size_t TMaximumObservers = 8>
class StateSubscriberRegistry final {
    static_assert(TMaximumSubscribers > 0, "StateSubscriberRegistry subscriber capacity must be non-zero");
    static_assert(TMaximumObservers > 0, "StateSubscriberRegistry observer capacity must be non-zero");

    static constexpr auto ExternalPreferred = System::Memory::MemoryPolicy::ExternalPreferred;

    class RegistryObservable final : public Observable::ThreadSafeObservable {
    public:
        void Added(const DeviceIdentifier& device, StateTypeId typeId) {
            ExecuteNotification([&](NotificationContext& notification) {
                notification.WithObservers<IStateSubscriberRegistryObserver>([&](IStateSubscriberRegistryObserver* observer) {
                    observer->OnRemoteStateSubscriberAdded(device, typeId);
                });
            });
        }
        void Removed(const DeviceIdentifier& device, StateTypeId typeId) {
            ExecuteNotification([&](NotificationContext& notification) {
                notification.WithObservers<IStateSubscriberRegistryObserver>([&](IStateSubscriberRegistryObserver* observer) {
                    observer->OnRemoteStateSubscriberRemoved(device, typeId);
                });
            });
        }
        void DeviceRemoved(const DeviceIdentifier& device) {
            ExecuteNotification([&](NotificationContext& notification) {
                notification.WithObservers<IStateSubscriberRegistryObserver>([&](IStateSubscriberRegistryObserver* observer) {
                    observer->OnRemoteStateSubscriberDeviceRemoved(device);
                });
            });
        }
        void CapacityExhausted(const DeviceIdentifier& device, StateTypeId typeId) {
            ExecuteNotification([&](NotificationContext& notification) {
                notification.WithObservers<IStateSubscriberRegistryObserver>([&](IStateSubscriberRegistryObserver* observer) {
                    observer->OnRemoteStateSubscriberCapacityExhausted(device, typeId);
                });
            });
        }
    };

    struct SubscriberRecord {
        bool Used = false;
        DeviceIdentifier Device{};
        std::array<bool, TContract::StateCount> States{};
    };

    using SubscriberStorage = System::Memory::Vector<SubscriberRecord, ExternalPreferred>;
    using DeviceSnapshotStorage = System::Memory::Vector<DeviceIdentifier, ExternalPreferred>;
    using TypeSnapshotStorage = System::Memory::Vector<StateTypeId, ExternalPreferred>;

    mutable SubscriberStorage _subscribers;
    mutable std::mutex _mutex;
    mutable std::mutex _observerMutex;
    std::shared_ptr<RegistryObservable> _observable;

    bool EnsureSubscribers() const {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_subscribers.size() == TMaximumSubscribers) return true;
        try {
            _subscribers.assign(TMaximumSubscribers, SubscriberRecord{});
            return true;
        } catch (...) {
            return false;
        }
    }

    bool EnsureObservable() {
        std::lock_guard<std::mutex> lock(_observerMutex);
        if (_observable) return true;
        try {
            _observable = System::Memory::MakeShared<RegistryObservable, ExternalPreferred>();
            return static_cast<bool>(_observable);
        } catch (...) {
            return false;
        }
    }

    std::shared_ptr<RegistryObservable> ObservableSnapshot() const {
        std::lock_guard<std::mutex> lock(_observerMutex);
        return _observable;
    }

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
    static constexpr std::size_t MaximumObservers = TMaximumObservers;

    Observable::ObserverHandlePtr RegisterObserver(IStateSubscriberRegistryObserver* observer) {
        if (observer == nullptr || !EnsureObservable()) return {};
        std::lock_guard<std::mutex> lock(_observerMutex);
        if (_observable->GetObserverCount() >= TMaximumObservers) return {};
        return _observable->template RegisterObserverAs<IStateSubscriberRegistryObserver>(observer);
    }

    void UnregisterObserver(IStateSubscriberRegistryObserver* observer) {
        auto observable = ObservableSnapshot();
        if (observable) observable->UnregisterObserver(observer);
    }

    bool Subscribe(const DeviceIdentifier& device, StateTypeId typeId) {
        std::size_t index = 0;
        if (!TContract::TryIndexOf(typeId, index) || !EnsureSubscribers()) return false;

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

        auto observable = ObservableSnapshot();
        if (capacityExhausted) {
            if (observable) observable->CapacityExhausted(device, typeId);
            return false;
        }
        if (added && observable) observable->Added(device, typeId);
        return true;
    }

    bool Unsubscribe(const DeviceIdentifier& device, StateTypeId typeId) {
        std::size_t index = 0;
        if (!TContract::TryIndexOf(typeId, index) || !EnsureSubscribers()) return false;

        bool removed = false;
        {
            std::lock_guard<std::mutex> lock(_mutex);
            auto* subscriber = FindLocked(device);
            if (subscriber == nullptr || !subscriber->States[index]) return false;
            subscriber->States[index] = false;
            removed = true;
        }
        auto observable = ObservableSnapshot();
        if (removed && observable) observable->Removed(device, typeId);
        return removed;
    }

    bool IsSubscribed(const DeviceIdentifier& device, StateTypeId typeId) const {
        std::size_t index = 0;
        if (!TContract::TryIndexOf(typeId, index) || !EnsureSubscribers()) return false;
        std::lock_guard<std::mutex> lock(_mutex);
        for (const auto& subscriber : _subscribers) {
            if (subscriber.Used && subscriber.Device == device) return subscriber.States[index];
        }
        return false;
    }

    template<typename TDefinition>
    bool IsSubscribed(const DeviceIdentifier& device) const {
        return IsSubscribed(device, StateTypeIdOf<TDefinition>);
    }

    bool HasSubscribers(StateTypeId typeId) const {
        std::size_t index = 0;
        if (!TContract::TryIndexOf(typeId, index) || !EnsureSubscribers()) return false;
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
        if (!EnsureSubscribers()) return false;
        bool removed = false;
        {
            std::lock_guard<std::mutex> lock(_mutex);
            auto* subscriber = FindLocked(device);
            if (subscriber == nullptr) return false;
            *subscriber = SubscriberRecord{};
            removed = true;
        }
        auto observable = ObservableSnapshot();
        if (removed && observable) observable->DeviceRemoved(device);
        return removed;
    }

    template<typename TCallback>
    void ForEachSubscriber(StateTypeId typeId, TCallback&& callback) const {
        std::size_t index = 0;
        if (!TContract::TryIndexOf(typeId, index) || !EnsureSubscribers()) return;
        DeviceSnapshotStorage matches;
        try {
            matches.reserve(TMaximumSubscribers);
        } catch (...) {
            return;
        }
        {
            std::lock_guard<std::mutex> lock(_mutex);
            for (const auto& subscriber : _subscribers) {
                if (subscriber.Used && subscriber.States[index]) matches.push_back(subscriber.Device);
            }
        }
        for (const auto& device : matches) callback(device);
    }

    template<typename TCallback>
    void ForEachSubscribedType(const DeviceIdentifier& device, TCallback&& callback) const {
        if (!EnsureSubscribers()) return;
        TypeSnapshotStorage types;
        try {
            types.reserve(TContract::StateCount);
        } catch (...) {
            return;
        }
        {
            std::lock_guard<std::mutex> lock(_mutex);
            for (const auto& subscriber : _subscribers) {
                if (!subscriber.Used || subscriber.Device != device) continue;
                for (std::size_t index = 0; index < TContract::StateCount; ++index) {
                    if (subscriber.States[index]) types.push_back(TContract::TypeIds[index]);
                }
                break;
            }
        }
        for (StateTypeId type : types) callback(type);
    }
};

} // namespace State
} // namespace ESPressio
