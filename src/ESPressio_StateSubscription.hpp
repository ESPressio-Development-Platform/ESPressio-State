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

enum class StateSubscriptionScope : uint8_t {
    AnyDevice = 0,
    SpecificDevice
};

template<typename TDefinition>
struct StateSubscription final {
    static constexpr StateTypeId TypeId = StateTypeIdOf<TDefinition>;
    StateSubscriptionScope Scope = StateSubscriptionScope::AnyDevice;
    DeviceIdentifier Device{};

    static constexpr StateSubscription Any() { return StateSubscription{}; }
    static constexpr StateSubscription From(const DeviceIdentifier& device) {
        StateSubscription result;
        result.Scope = StateSubscriptionScope::SpecificDevice;
        result.Device = device;
        return result;
    }
    bool Matches(const DeviceIdentifier& device) const noexcept {
        return Scope == StateSubscriptionScope::AnyDevice || Device == device;
    }
};

template<std::size_t TCapacity>
class StateSubscriptionRegistry final {
public:
    struct Descriptor {
        StateTypeId TypeId = 0;
        StateSubscriptionScope Scope = StateSubscriptionScope::AnyDevice;
        DeviceIdentifier Device{};
    };

private:
    class RegistryObservable final : public Observable::ThreadSafeObservable {
    public:
        void Subscribed(StateTypeId typeId, StateSubscriptionScope scope, const DeviceIdentifier& device) {
            ExecuteNotification([&](NotificationContext& notification) {
                notification.WithObservers<IStateSubscriptionRegistryObserver>([&](IStateSubscriptionRegistryObserver* observer) {
                    observer->OnStateSubscribed(typeId, scope, device);
                });
            });
        }
        void Unsubscribed(StateTypeId typeId, StateSubscriptionScope scope, const DeviceIdentifier& device) {
            ExecuteNotification([&](NotificationContext& notification) {
                notification.WithObservers<IStateSubscriptionRegistryObserver>([&](IStateSubscriptionRegistryObserver* observer) {
                    observer->OnStateUnsubscribed(typeId, scope, device);
                });
            });
        }
        void CapacityExhausted(StateTypeId typeId, StateSubscriptionScope scope, const DeviceIdentifier& device) {
            ExecuteNotification([&](NotificationContext& notification) {
                notification.WithObservers<IStateSubscriptionRegistryObserver>([&](IStateSubscriptionRegistryObserver* observer) {
                    observer->OnStateSubscriptionCapacityExhausted(typeId, scope, device);
                });
            });
        }
    };

    struct Record {
        bool Used = false;
        Descriptor Subscription{};
    };

    std::array<Record, TCapacity> _records{};
    mutable std::mutex _mutex;
    std::shared_ptr<RegistryObservable> _observable = std::make_shared<RegistryObservable>();

public:
    static constexpr std::size_t Capacity = TCapacity;

    Observable::ObserverHandlePtr RegisterObserver(IStateSubscriptionRegistryObserver* observer) {
        return _observable->template RegisterObserverAs<IStateSubscriptionRegistryObserver>(observer);
    }

    void UnregisterObserver(IStateSubscriptionRegistryObserver* observer) {
        _observable->UnregisterObserver(observer);
    }

    template<typename TDefinition>
    bool Subscribe(const StateSubscription<TDefinition>& subscription = StateSubscription<TDefinition>::Any()) {
        const Descriptor descriptor{StateTypeIdOf<TDefinition>, subscription.Scope, subscription.Device};
        bool added = false;
        bool capacityExhausted = false;
        {
            std::lock_guard<std::mutex> lock(_mutex);
            for (const auto& record : _records) {
                if (record.Used && record.Subscription.TypeId == descriptor.TypeId &&
                    record.Subscription.Scope == descriptor.Scope && record.Subscription.Device == descriptor.Device) {
                    return true;
                }
            }
            for (auto& record : _records) {
                if (!record.Used) {
                    record.Used = true;
                    record.Subscription = descriptor;
                    added = true;
                    break;
                }
            }
            if (!added) capacityExhausted = true;
        }

        // Observer/transport code may synchronously call back into the registry
        // (for example when a Subscribe immediately produces an initial State
        // snapshot). Never execute that code while the registry mutex is held.
        if (added) {
            _observable->Subscribed(descriptor.TypeId, descriptor.Scope, descriptor.Device);
            return true;
        }
        if (capacityExhausted) {
            _observable->CapacityExhausted(descriptor.TypeId, descriptor.Scope, descriptor.Device);
        }
        return false;
    }

    template<typename TDefinition>
    bool Unsubscribe(const StateSubscription<TDefinition>& subscription = StateSubscription<TDefinition>::Any()) {
        const Descriptor descriptor{StateTypeIdOf<TDefinition>, subscription.Scope, subscription.Device};
        bool removed = false;
        {
            std::lock_guard<std::mutex> lock(_mutex);
            for (auto& record : _records) {
                if (record.Used && record.Subscription.TypeId == descriptor.TypeId &&
                    record.Subscription.Scope == descriptor.Scope && record.Subscription.Device == descriptor.Device) {
                    record = Record{};
                    removed = true;
                    break;
                }
            }
        }
        if (removed) {
            _observable->Unsubscribed(descriptor.TypeId, descriptor.Scope, descriptor.Device);
        }
        return removed;
    }

    bool IsSubscribed(const DeviceIdentifier& device, StateTypeId typeId) const {
        std::lock_guard<std::mutex> lock(_mutex);
        for (const auto& record : _records) {
            if (record.Used && record.Subscription.TypeId == typeId &&
                (record.Subscription.Scope == StateSubscriptionScope::AnyDevice || record.Subscription.Device == device)) return true;
        }
        return false;
    }

    template<typename TDefinition>
    bool IsSubscribed(const DeviceIdentifier& device) const {
        return IsSubscribed(device, StateTypeIdOf<TDefinition>);
    }

    std::size_t Count() const {
        std::lock_guard<std::mutex> lock(_mutex);
        std::size_t count = 0;
        for (const auto& record : _records) if (record.Used) ++count;
        return count;
    }

    template<typename TCallback>
    void ForEach(TCallback&& callback) const {
        // Descriptor is a small fixed transport/configuration value. Copying only
        // active descriptors here intentionally preserves a lock-free callback
        // phase and avoids exposing mutable registry storage across re-entrant code.
        std::array<Descriptor, TCapacity> snapshot{};
        std::size_t count = 0;
        {
            std::lock_guard<std::mutex> lock(_mutex);
            for (const auto& record : _records) {
                if (record.Used) snapshot[count++] = record.Subscription;
            }
        }
        for (std::size_t index = 0; index < count; ++index) callback(snapshot[index]);
    }
};

} // namespace State
} // namespace ESPressio
