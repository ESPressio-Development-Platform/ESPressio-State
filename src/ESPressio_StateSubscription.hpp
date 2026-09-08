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
/**
 * ESPressio Memory Audit
 * Underlying storage: 1 bytes
 * Total Memory: 1 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
enum class StateSubscriptionScope : uint8_t {
    AnyDevice = 0,
    SpecificDevice
};

/// <summary>Describes a typed subscription to state updates from any or one specific remote device.</summary>
/// <typeparam name="TDefinition">State definition being subscribed to.</typeparam>
/**
 * ESPressio Memory Audit
 * Members:
 * - Scope (StateSubscriptionScope): 1 bytes [0 bytes dynamic allocation]
 * - Device (DeviceIdentifier): 16 bytes [0 bytes dynamic allocation]
 * Total Memory: 17 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
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

/// <summary>Maintains a bounded registry of local remote-state subscription interests.</summary>
/// <typeparam name="TCapacity">Maximum number of retained subscription descriptors.</typeparam>
/// <typeparam name="TMaximumObservers">Maximum simultaneous lifecycle observer registrations.</typeparam>
/// <remarks>Construction is allocation-free. Bounded record storage and callback snapshots prefer external memory and are materialized only when registry operations require them. Optional observer infrastructure is allocated only when an observer is registered, and observer registration is independently bounded.</remarks>
/**
 * ESPressio Memory Audit
 * Members:
 * - _records (RecordStorage): 12 bytes [Capacity * (32 bytes) element storage]
 * - _mutex (std::mutex): 4 bytes [native synchronization state may allocate platform resources lazily]
 * - _observerMutex (std::mutex): 4 bytes [native synchronization state may allocate platform resources lazily]
 * - _observable (std::shared_ptr<RegistryObservable>): 8 bytes [shared control block (~12+ bytes; allocate_shared may co-locate object) + object 96 bytes; pointee: ThreadSafeObservable: Observable: IUntypedObservable: IObservable: enable_shared_from_this: embedded weak_ptr shares a control block when activated; pointee: ThreadSafeObservable: Observable: IUntypedObservable: IObservable: _lifetimeControl: shared control block (~12+ bytes; allocate_shared may co-locate object) + object 20 bytes; pointee: ThreadSafeObservable: Observable: IUntypedObservable: IObservable: _lifetimeControl: pointee: _mutex: native synchronization state may allocate platform resources lazily; pointee: ThreadSafeObservable: Observable: IUntypedObservable: IObservable: _lifetimeControl: pointee: _condition: native condition-variable state may allocate platform synchronization resources; pointee: ThreadSafeObservable: Observable: _registrations: Capacity * (12 bytes) element storage; pointee: ThreadSafeObservable: Observable: _bindings: Capacity * (12 bytes) element storage; pointee: ThreadSafeObservable: _mutex: _owned: owned object: 4 bytes; pointee: ThreadSafeObservable: _mutex: _fallback: _mutex: native synchronization state may allocate platform resources lazily; pointee: ThreadSafeObservable: _notificationMutex: _owned: owned object: 4 bytes; pointee: ThreadSafeObservable: _notificationMutex: _fallback: _mutex: native synchronization state may allocate platform resources lazily]
 * Total Memory: 28 bytes [_records: Capacity * (32 bytes) element storage; _mutex: native synchronization state may allocate platform resources lazily; _observerMutex: native synchronization state may allocate platform resources lazily; _observable: shared control block (~12+ bytes; allocate_shared may co-locate object) + object 96 bytes; _observable: pointee: ThreadSafeObservable: Observable: IUntypedObservable: IObservable: enable_shared_from_this: embedded weak_ptr shares a control block when activated; _observable: pointee: ThreadSafeObservable: Observable: IUntypedObservable: IObservable: _lifetimeControl: shared control block (~12+ bytes; allocate_shared may co-locate object) + object 20 bytes; _observable: pointee: ThreadSafeObservable: Observable: IUntypedObservable: IObservable: _lifetimeControl: pointee: _mutex: native synchronization state may allocate platform resources lazily; _observable: pointee: ThreadSafeObservable: Observable: IUntypedObservable: IObservable: _lifetimeControl: pointee: _condition: native condition-variable state may allocate platform synchronization resources; _observable: pointee: ThreadSafeObservable: Observable: _registrations: Capacity * (12 bytes) element storage; _observable: pointee: ThreadSafeObservable: Observable: _bindings: Capacity * (12 bytes) element storage; _observable: pointee: ThreadSafeObservable: _mutex: _owned: owned object: 4 bytes; _observable: pointee: ThreadSafeObservable: _mutex: _fallback: _mutex: native synchronization state may allocate platform resources lazily; _observable: pointee: ThreadSafeObservable: _notificationMutex: _owned: owned object: 4 bytes; _observable: pointee: ThreadSafeObservable: _notificationMutex: _fallback: _mutex: native synchronization state may allocate platform resources lazily]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * Confidence: medium; compile-time sizeof on the concrete target remains authoritative for ABI-sensitive/opaque members.
 * End ESPressio Memory Audit
 */
template<std::size_t TCapacity, std::size_t TMaximumObservers = 8>
class StateSubscriptionRegistry final {
    static_assert(TCapacity > 0, "StateSubscriptionRegistry capacity must be non-zero");
    static_assert(TMaximumObservers > 0, "StateSubscriptionRegistry observer capacity must be non-zero");

public:
/**
 * ESPressio Memory Audit
 * Members:
 * - TypeId (StateTypeId): 8 bytes [0 bytes dynamic allocation]
 * - Scope (StateSubscriptionScope): 1 bytes [0 bytes dynamic allocation]
 * - Device (DeviceIdentifier): 16 bytes [0 bytes dynamic allocation]
 * Total Memory: 28 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
struct Descriptor {
        StateTypeId TypeId = 0;
        StateSubscriptionScope Scope = StateSubscriptionScope::AnyDevice;
        DeviceIdentifier Device{};
    };

private:
    static constexpr auto ExternalPreferred = System::Memory::MemoryPolicy::ExternalPreferred;

/**
 * ESPressio Memory Audit
 * Inherited Memory Total: 96 bytes [ThreadSafeObservable: Observable: IUntypedObservable: IObservable: enable_shared_from_this: embedded weak_ptr shares a control block when activated; ThreadSafeObservable: Observable: IUntypedObservable: IObservable: _lifetimeControl: shared control block (~12+ bytes; allocate_shared may co-locate object) + object 20 bytes; ThreadSafeObservable: Observable: IUntypedObservable: IObservable: _lifetimeControl: pointee: _mutex: native synchronization state may allocate platform resources lazily; ThreadSafeObservable: Observable: IUntypedObservable: IObservable: _lifetimeControl: pointee: _condition: native condition-variable state may allocate platform synchronization resources; ThreadSafeObservable: Observable: _registrations: Capacity * (12 bytes) element storage; ThreadSafeObservable: Observable: _bindings: Capacity * (12 bytes) element storage; ThreadSafeObservable: _mutex: _owned: owned object: 4 bytes; ThreadSafeObservable: _mutex: _fallback: _mutex: native synchronization state may allocate platform resources lazily; ThreadSafeObservable: _notificationMutex: _owned: owned object: 4 bytes; ThreadSafeObservable: _notificationMutex: _fallback: _mutex: native synchronization state may allocate platform resources lazily]
 * Members: none (standalone empty object occupies 1 byte; an eligible empty base may be optimized to 0 bytes).
 * Total Memory: 96 bytes [ThreadSafeObservable: Observable: IUntypedObservable: IObservable: enable_shared_from_this: embedded weak_ptr shares a control block when activated; ThreadSafeObservable: Observable: IUntypedObservable: IObservable: _lifetimeControl: shared control block (~12+ bytes; allocate_shared may co-locate object) + object 20 bytes; ThreadSafeObservable: Observable: IUntypedObservable: IObservable: _lifetimeControl: pointee: _mutex: native synchronization state may allocate platform resources lazily; ThreadSafeObservable: Observable: IUntypedObservable: IObservable: _lifetimeControl: pointee: _condition: native condition-variable state may allocate platform synchronization resources; ThreadSafeObservable: Observable: _registrations: Capacity * (12 bytes) element storage; ThreadSafeObservable: Observable: _bindings: Capacity * (12 bytes) element storage; ThreadSafeObservable: _mutex: _owned: owned object: 4 bytes; ThreadSafeObservable: _mutex: _fallback: _mutex: native synchronization state may allocate platform resources lazily; ThreadSafeObservable: _notificationMutex: _owned: owned object: 4 bytes; ThreadSafeObservable: _notificationMutex: _fallback: _mutex: native synchronization state may allocate platform resources lazily]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
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

/**
 * ESPressio Memory Audit
 * Members:
 * - Used (bool): 1 bytes [0 bytes dynamic allocation]
 * - Subscription (Descriptor): 28 bytes [0 bytes dynamic allocation]
 * Total Memory: 32 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
struct Record {
        bool Used = false;
        Descriptor Subscription{};
    };

    using RecordStorage = System::Memory::Vector<Record, ExternalPreferred>;
    using SnapshotStorage = System::Memory::Vector<Descriptor, ExternalPreferred>;

    mutable RecordStorage _records;
    mutable std::mutex _mutex;
    mutable std::mutex _observerMutex;
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
        std::lock_guard<std::mutex> lock(_observerMutex);
        if (_observable) return true;
        try {
            _observable = System::Memory::MakeShared<RegistryObservable, ExternalPreferred>();
            return static_cast<bool>(_observable);
        } catch (...) {
            return false;
        }
    }

public:
    static constexpr std::size_t Capacity = TCapacity;
    static constexpr std::size_t MaximumObservers = TMaximumObservers;

    Observable::ObserverHandlePtr RegisterObserver(IStateSubscriptionRegistryObserver* observer) {
        if (observer == nullptr || !EnsureObservable()) return {};
        std::lock_guard<std::mutex> lock(_observerMutex);
        if (_observable->GetObserverCount() >= TMaximumObservers) return {};
        return _observable->template RegisterObserverAs<IStateSubscriptionRegistryObserver>(observer);
    }

    void UnregisterObserver(IStateSubscriptionRegistryObserver* observer) {
        std::shared_ptr<RegistryObservable> observable;
        {
            std::lock_guard<std::mutex> lock(_observerMutex);
            observable = _observable;
        }
        if (observable) observable->UnregisterObserver(observer);
    }

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

        std::shared_ptr<RegistryObservable> observable;
        {
            std::lock_guard<std::mutex> lock(_observerMutex);
            observable = _observable;
        }
        if (added) {
            if (observable) observable->Subscribed(descriptor.TypeId, descriptor.Scope, descriptor.Device);
            return true;
        }
        if (capacityExhausted && observable) {
            observable->CapacityExhausted(descriptor.TypeId, descriptor.Scope, descriptor.Device);
        }
        return false;
    }

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
        if (removed) {
            std::shared_ptr<RegistryObservable> observable;
            {
                std::lock_guard<std::mutex> lock(_observerMutex);
                observable = _observable;
            }
            if (observable) observable->Unsubscribed(descriptor.TypeId, descriptor.Scope, descriptor.Device);
        }
        return removed;
    }

    bool IsSubscribed(const DeviceIdentifier& device, StateTypeId typeId) const {
        if (!EnsureRecords()) return false;
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
        if (!EnsureRecords()) return 0;
        std::lock_guard<std::mutex> lock(_mutex);
        std::size_t count = 0;
        for (const auto& record : _records) if (record.Used) ++count;
        return count;
    }

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
