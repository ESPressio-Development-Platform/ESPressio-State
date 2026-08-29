#pragma once

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

/// <summary>Specifies whether a state subscription applies to every remote device or one specific device.</summary>
enum class StateSubscriptionScope : uint8_t {
    AnyDevice = 0,
    SpecificDevice
};

/// <summary>Describes a typed subscription to state updates from any or one specific remote device.</summary>
/// <typeparam name="TDefinition">State definition being subscribed to.</typeparam>
template<typename TDefinition>
struct StateSubscription final {
    /// <summary>Stable type identifier of the subscribed state definition.</summary>
    static constexpr StateTypeId TypeId = StateTypeIdOf<TDefinition>;
    /// <summary>Device scope applied to the subscription.</summary>
    StateSubscriptionScope Scope = StateSubscriptionScope::AnyDevice;
    /// <summary>Specific device when <c>Scope</c> is <c>SpecificDevice</c>.</summary>
    DeviceIdentifier Device{};

    /// <summary>Creates a subscription accepting the state from any remote device.</summary>
    static constexpr StateSubscription Any() { return StateSubscription{}; }
    /// <summary>Creates a subscription restricted to one remote device.</summary>
    static constexpr StateSubscription From(const DeviceIdentifier& device) {
        StateSubscription result;
        result.Scope = StateSubscriptionScope::SpecificDevice;
        result.Device = device;
        return result;
    }
    /// <summary>Determines whether a remote device matches this subscription's scope.</summary>
    bool Matches(const DeviceIdentifier& device) const noexcept {
        return Scope == StateSubscriptionScope::AnyDevice || Device == device;
    }
};

/// <summary>Maintains a bounded registry of local remote-state subscription interests.</summary>
/// <typeparam name="TCapacity">Maximum number of retained subscription descriptors.</typeparam>
/// <remarks>Construction is allocation-free. Bounded record storage and callback snapshots prefer external memory and are materialized only when registry operations require them. Optional observer infrastructure is allocated only when an observer is registered.</remarks>
template<std::size_t TCapacity>
class StateSubscriptionRegistry final {
public:
    /// <summary>Runtime description of one registered state subscription.</summary>
    struct Descriptor {
        /// <summary>Stable state type identifier.</summary>
        StateTypeId TypeId = 0;
        /// <summary>Device scope associated with the subscription.</summary>
        StateSubscriptionScope Scope = StateSubscriptionScope::AnyDevice;
        /// <summary>Specific device when the scope is device-specific.</summary>
        DeviceIdentifier Device{};
    };

private:
    static constexpr auto ExternalPreferred =
        System::Memory::MemoryPolicy::ExternalPreferred;

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

    using RecordStorage = System::Memory::Vector<Record, ExternalPreferred>;
    using SnapshotStorage = System::Memory::Vector<Descriptor, ExternalPreferred>;

    mutable RecordStorage _records;
    mutable std::mutex _mutex;
    std::shared_ptr<RegistryObservable> _observable;

    bool EnsureRecords() const {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_records.size() == TCapacity) return true;
        try {
            _records.assign(TCapacity, Record{});
            return true;
        } catch (...) {
            return false;
        }
    }

    bool EnsureObservable() {
        if (_observable) return true;
        try {
            _observable = System::Memory::MakeShared<
                RegistryObservable,
                ExternalPreferred
            >();
            return static_cast<bool>(_observable);
        } catch (...) {
            return false;
        }
    }

public:
    /// <summary>Maximum number of subscriptions retained by this registry.</summary>
    static constexpr std::size_t Capacity = TCapacity;

    /// <summary>Registers an observer for subscription-registry lifecycle events, allocating observer infrastructure on demand.</summary>
    Observable::ObserverHandlePtr RegisterObserver(IStateSubscriptionRegistryObserver* observer) {
        if (!EnsureObservable()) return {};
        return _observable->template RegisterObserverAs<IStateSubscriptionRegistryObserver>(observer);
    }

    /// <summary>Unregisters a subscription-registry observer.</summary>
    void UnregisterObserver(IStateSubscriptionRegistryObserver* observer) {
        if (_observable) _observable->UnregisterObserver(observer);
    }

    /// <summary>Adds a typed state subscription when it is not already present and capacity permits.</summary>
    /// <typeparam name="TDefinition">State definition to subscribe to.</typeparam>
    /// <returns><c>true</c> when the subscription is present after the call.</returns>
    template<typename TDefinition>
    bool Subscribe(const StateSubscription<TDefinition>& subscription = StateSubscription<TDefinition>::Any()) {
        if (!EnsureRecords()) return false;
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
            if (_observable) {
                _observable->Subscribed(descriptor.TypeId, descriptor.Scope, descriptor.Device);
            }
            return true;
        }
        if (capacityExhausted && _observable) {
            _observable->CapacityExhausted(descriptor.TypeId, descriptor.Scope, descriptor.Device);
        }
        return false;
    }

    /// <summary>Removes a typed state subscription when present.</summary>
    /// <typeparam name="TDefinition">State definition to unsubscribe from.</typeparam>
    template<typename TDefinition>
    bool Unsubscribe(const StateSubscription<TDefinition>& subscription = StateSubscription<TDefinition>::Any()) {
        if (!EnsureRecords()) return false;
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
        if (removed && _observable) {
            _observable->Unsubscribed(descriptor.TypeId, descriptor.Scope, descriptor.Device);
        }
        return removed;
    }

    /// <summary>Determines whether the specified state type is subscribed for a remote device.</summary>
    bool IsSubscribed(const DeviceIdentifier& device, StateTypeId typeId) const {
        if (!EnsureRecords()) return false;
        std::lock_guard<std::mutex> lock(_mutex);
        for (const auto& record : _records) {
            if (record.Used && record.Subscription.TypeId == typeId &&
                (record.Subscription.Scope == StateSubscriptionScope::AnyDevice || record.Subscription.Device == device)) return true;
        }
        return false;
    }

    /// <summary>Determines whether a typed state definition is subscribed for a remote device.</summary>
    template<typename TDefinition>
    bool IsSubscribed(const DeviceIdentifier& device) const {
        return IsSubscribed(device, StateTypeIdOf<TDefinition>);
    }

    /// <summary>Gets the number of active subscriptions.</summary>
    std::size_t Count() const {
        if (!EnsureRecords()) return 0;
        std::lock_guard<std::mutex> lock(_mutex);
        std::size_t count = 0;
        for (const auto& record : _records) if (record.Used) ++count;
        return count;
    }

    /// <summary>Invokes a callback for a stable externally backed snapshot of each active subscription descriptor.</summary>
    template<typename TCallback>
    void ForEach(TCallback&& callback) const {
        if (!EnsureRecords()) return;
        SnapshotStorage snapshot;
        try {
            snapshot.reserve(TCapacity);
        } catch (...) {
            return;
        }
        {
            std::lock_guard<std::mutex> lock(_mutex);
            for (const auto& record : _records) {
                if (record.Used) snapshot.push_back(record.Subscription);
            }
        }
        for (const auto& descriptor : snapshot) callback(descriptor);
    }
};

} // namespace State
} // namespace ESPressio
