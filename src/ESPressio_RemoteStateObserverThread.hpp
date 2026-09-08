#pragma once

#if !__has_include(<ESPressio_PrecisionThread.hpp>)
#error "RemoteStateObserverThread requires ESPressio Threads. Include the Threads working branch when using this optional execution layer."
#endif

#include <cstddef>
#include <memory>
#include <mutex>
#include <tuple>
#include <utility>

#include <ESPressio_Memory.hpp>
#include <ESPressio_PrecisionThread.hpp>
#include <ESPressio_PrecisionThreadTraits.hpp>
#include <ESPressio_ThreadSafeObservable.hpp>

#include "ESPressio_RemoteStateManager.hpp"
#include "ESPressio_StateObservers.hpp"

#ifndef ESPRESSIO_STATE_OBSERVER_THREAD_STACK_SIZE
#define ESPRESSIO_STATE_OBSERVER_THREAD_STACK_SIZE 4096
#endif
#ifndef ESPRESSIO_STATE_OBSERVER_THREAD_PRIORITY
#define ESPRESSIO_STATE_OBSERVER_THREAD_PRIORITY 2
#endif

namespace ESPressio {
namespace State {

/// <summary>Coalesces remote State changes and dispatches application observers from an ESPressio PrecisionThread.</summary>
/// <typeparam name="TContract">State contract accepted by the observed manager.</typeparam>
/// <typeparam name="TMaximumDevices">Maximum number of manager devices represented by bounded dirty/delivered storage.</typeparam>
/// <typeparam name="TMaximumObservers">Maximum simultaneous application observer registrations.</typeparam>
/**
 * ESPressio Memory Audit
 * Inherited Memory Total: 360 bytes [PrecisionThread: Thread: _taskExited: owned object: 4 bytes; PrecisionThread: Thread: _taskStartGate: owned object: 4 bytes; PrecisionThread: Thread: _taskConfigurationMutex: _owned: owned object: 4 bytes; PrecisionThread: Thread: _taskConfigurationMutex: _fallback: _mutex: native synchronization state may allocate platform resources lazily; PrecisionThread: Thread: _stateTransitionMutex: _owned: owned object: 4 bytes; PrecisionThread: Thread: _stateTransitionMutex: _fallback: _mutex: native synchronization state may allocate platform resources lazily; PrecisionThread: Thread: _lifecycleObservable: shared control block (~12+ bytes; allocate_shared may co-locate object) + object 96 bytes; PrecisionThread: Thread: _lifecycleObservable: pointee: ThreadSafeObservable: Observable: IUntypedObservable: IObservable: enable_shared_from_this: embedded weak_ptr shares a control block when activated; PrecisionThread: Thread: _lifecycleObservable: pointee: ThreadSafeObservable: Observable: IUntypedObservable: IObservable: _lifetimeControl: shared control block (~12+ bytes; allocate_shared may co-locate object) + object 20 bytes; PrecisionThread: Thread: _lifecycleObservable: pointee: ThreadSafeObservable: Observable: IUntypedObservable: IObservable: _lifetimeControl: pointee: _mutex: native synchronization state may allocate platform resources lazily; PrecisionThread: Thread: _lifecycleObservable: pointee: ThreadSafeObservable: Observable: IUntypedObservable: IObservable: _lifetimeControl: pointee: _condition: native condition-variable state may allocate platform synchronization resources; PrecisionThread: Thread: _lifecycleObservable: pointee: ThreadSafeObservable: Observable: _registrations: Capacity * (12 bytes) element storage; PrecisionThread: Thread: _lifecycleObservable: pointee: ThreadSafeObservable: Observable: _bindings: Capacity * (12 bytes) element storage; PrecisionThread: Thread: _lifecycleObservable: pointee: ThreadSafeObservable: _mutex: _owned: owned object: 4 bytes; PrecisionThread: Thread: _lifecycleObservable: pointee: ThreadSafeObservable: _mutex: _fallback: _mutex: native synchronization state may allocate platform resources lazily; PrecisionThread: Thread: _lifecycleObservable: pointee: ThreadSafeObservable: _notificationMutex: _owned: owned object: 4 bytes; PrecisionThread: Thread: _lifecycleObservable: pointee: ThreadSafeObservable: _notificationMutex: _fallback: _mutex: native synchronization state may allocate platform resources lazily; PrecisionThread: Thread: _callbackMutex: _owned: owned object: 4 bytes; PrecisionThread: Thread: _callbackMutex: _fallback: _mutex: native synchronization state may allocate platform resources lazily; PrecisionThread: Thread: _onDestroy: shared control block (~12+ bytes; allocate_shared may co-locate object) + object 4 bytes; PrecisionThread: Thread: _onInitialize: shared control block (~12+ bytes; allocate_shared may co-locate object) + object 4 bytes; PrecisionThread: Thread: _onStart: shared control block (~12+ bytes; allocate_shared may co-locate object) + object 4 bytes; PrecisionThread: Thread: _onPause: shared control block (~12+ bytes; allocate_shared may co-locate object) + object 4 bytes; PrecisionThread: Thread: _onTerminate: shared control block (~12+ bytes; allocate_shared may co-locate object) + object 4 bytes; PrecisionThread: Thread: _onTerminated: shared control block (~12+ bytes; allocate_shared may co-locate object) + object 4 bytes; PrecisionThread: Thread: _onInitializationFailed: shared control block (~12+ bytes; allocate_shared may co-locate object) + object 4 bytes; PrecisionThread: Thread: _onExecutionFailed: shared control block (~12+ bytes; allocate_shared may co-locate object) + object 4 bytes; PrecisionThread: Thread: _onStateChange: shared control block (~12+ bytes; allocate_shared may co-locate object) + object 4 bytes; PrecisionThread: _iterationObservable: shared control block (~12+ bytes; allocate_shared may co-locate object) + object 96 bytes; PrecisionThread: _iterationObservable: pointee: ThreadSafeObservable: Observable: IUntypedObservable: IObservable: enable_shared_from_this: embedded weak_ptr shares a control block when activated; PrecisionThread: _iterationObservable: pointee: ThreadSafeObservable: Observable: IUntypedObservable: IObservable: _lifetimeControl: shared control block (~12+ bytes; allocate_shared may co-locate object) + object 20 bytes; PrecisionThread: _iterationObservable: pointee: ThreadSafeObservable: Observable: IUntypedObservable: IObservable: _lifetimeControl: pointee: _mutex: native synchronization state may allocate platform resources lazily; PrecisionThread: _iterationObservable: pointee: ThreadSafeObservable: Observable: IUntypedObservable: IObservable: _lifetimeControl: pointee: _condition: native condition-variable state may allocate platform synchronization resources; PrecisionThread: _iterationObservable: pointee: ThreadSafeObservable: Observable: _registrations: Capacity * (12 bytes) element storage; PrecisionThread: _iterationObservable: pointee: ThreadSafeObservable: Observable: _bindings: Capacity * (12 bytes) element storage; PrecisionThread: _iterationObservable: pointee: ThreadSafeObservable: _mutex: _owned: owned object: 4 bytes; PrecisionThread: _iterationObservable: pointee: ThreadSafeObservable: _mutex: _fallback: _mutex: native synchronization state may allocate platform resources lazily; PrecisionThread: _iterationObservable: pointee: ThreadSafeObservable: _notificationMutex: _owned: owned object: 4 bytes; PrecisionThread: _iterationObservable: pointee: ThreadSafeObservable: _notificationMutex: _fallback: _mutex: native synchronization state may allocate platform resources lazily; PrecisionThread: _scheduleSignal: owned object: 4 bytes; PrecisionThread: _timingMutex: _owned: owned object: 4 bytes; PrecisionThread: _timingMutex: _fallback: _mutex: native synchronization state may allocate platform resources lazily; PrecisionThread: _iterationSamples: implementation blocks containing N * (8 bytes) plus block-map pointers]
 * Requires Stack/Heap Preallocation
 * Members:
 * - _manager (Manager&): 4 bytes [0 bytes dynamic allocation]
 * - _managerHandle (Observable::ObserverHandlePtr): 12 bytes [owned object: 4 bytes]
 * - _dirtyStates (DirtyStateStorage): 12 bytes [Capacity * (24 bytes) element storage]
 * - _processingStates (DirtyStateStorage): 12 bytes [Capacity * (24 bytes) element storage]
 * - _dirtyAvailability (DirtyAvailabilityStorage): 12 bytes [Capacity * (28 bytes) element storage]
 * - _processingAvailability (DirtyAvailabilityStorage): 12 bytes [Capacity * (28 bytes) element storage]
 * - _dirtyReachability (DirtyReachabilityStorage): 12 bytes [Capacity * (18 bytes) element storage]
 * - _processingReachability (DirtyReachabilityStorage): 12 bytes [Capacity * (18 bytes) element storage]
 * - _delivered (DeliveredStorage): 12 bytes [Capacity * (16 bytes known/aligned storage + sizeof(RemoteStateTuple<TContract>::Type) (target/toolchain dependent)) element storage]
 * - _observable (std::shared_ptr<DispatchObservable>): 8 bytes [shared control block (~12+ bytes; allocate_shared may co-locate object) + object 96 bytes; pointee: ThreadSafeObservable: Observable: IUntypedObservable: IObservable: enable_shared_from_this: embedded weak_ptr shares a control block when activated; pointee: ThreadSafeObservable: Observable: IUntypedObservable: IObservable: _lifetimeControl: shared control block (~12+ bytes; allocate_shared may co-locate object) + object 20 bytes; pointee: ThreadSafeObservable: Observable: IUntypedObservable: IObservable: _lifetimeControl: pointee: _mutex: native synchronization state may allocate platform resources lazily; pointee: ThreadSafeObservable: Observable: IUntypedObservable: IObservable: _lifetimeControl: pointee: _condition: native condition-variable state may allocate platform synchronization resources; pointee: ThreadSafeObservable: Observable: _registrations: Capacity * (12 bytes) element storage; pointee: ThreadSafeObservable: Observable: _bindings: Capacity * (12 bytes) element storage; pointee: ThreadSafeObservable: _mutex: _owned: owned object: 4 bytes; pointee: ThreadSafeObservable: _mutex: _fallback: _mutex: native synchronization state may allocate platform resources lazily; pointee: ThreadSafeObservable: _notificationMutex: _owned: owned object: 4 bytes; pointee: ThreadSafeObservable: _notificationMutex: _fallback: _mutex: native synchronization state may allocate platform resources lazily]
 * - _dirtyMutex (std::mutex): 4 bytes [native synchronization state may allocate platform resources lazily]
 * - _prepared (bool): 1 bytes [0 bytes dynamic allocation]
 * Total Memory: 476 bytes [PrecisionThread: Thread: _taskExited: owned object: 4 bytes; PrecisionThread: Thread: _taskStartGate: owned object: 4 bytes; PrecisionThread: Thread: _taskConfigurationMutex: _owned: owned object: 4 bytes; PrecisionThread: Thread: _taskConfigurationMutex: _fallback: _mutex: native synchronization state may allocate platform resources lazily; PrecisionThread: Thread: _stateTransitionMutex: _owned: owned object: 4 bytes; PrecisionThread: Thread: _stateTransitionMutex: _fallback: _mutex: native synchronization state may allocate platform resources lazily; PrecisionThread: Thread: _lifecycleObservable: shared control block (~12+ bytes; allocate_shared may co-locate object) + object 96 bytes; PrecisionThread: Thread: _lifecycleObservable: pointee: ThreadSafeObservable: Observable: IUntypedObservable: IObservable: enable_shared_from_this: embedded weak_ptr shares a control block when activated; PrecisionThread: Thread: _lifecycleObservable: pointee: ThreadSafeObservable: Observable: IUntypedObservable: IObservable: _lifetimeControl: shared control block (~12+ bytes; allocate_shared may co-locate object) + object 20 bytes; PrecisionThread: Thread: _lifecycleObservable: pointee: ThreadSafeObservable: Observable: IUntypedObservable: IObservable: _lifetimeControl: pointee: _mutex: native synchronization state may allocate platform resources lazily; PrecisionThread: Thread: _lifecycleObservable: pointee: ThreadSafeObservable: Observable: IUntypedObservable: IObservable: _lifetimeControl: pointee: _condition: native condition-variable state may allocate platform synchronization resources; PrecisionThread: Thread: _lifecycleObservable: pointee: ThreadSafeObservable: Observable: _registrations: Capacity * (12 bytes) element storage; PrecisionThread: Thread: _lifecycleObservable: pointee: ThreadSafeObservable: Observable: _bindings: Capacity * (12 bytes) element storage; PrecisionThread: Thread: _lifecycleObservable: pointee: ThreadSafeObservable: _mutex: _owned: owned object: 4 bytes; PrecisionThread: Thread: _lifecycleObservable: pointee: ThreadSafeObservable: _mutex: _fallback: _mutex: native synchronization state may allocate platform resources lazily; PrecisionThread: Thread: _lifecycleObservable: pointee: ThreadSafeObservable: _notificationMutex: _owned: owned object: 4 bytes; PrecisionThread: Thread: _lifecycleObservable: pointee: ThreadSafeObservable: _notificationMutex: _fallback: _mutex: native synchronization state may allocate platform resources lazily; PrecisionThread: Thread: _callbackMutex: _owned: owned object: 4 bytes; PrecisionThread: Thread: _callbackMutex: _fallback: _mutex: native synchronization state may allocate platform resources lazily; PrecisionThread: Thread: _onDestroy: shared control block (~12+ bytes; allocate_shared may co-locate object) + object 4 bytes; PrecisionThread: Thread: _onInitialize: shared control block (~12+ bytes; allocate_shared may co-locate object) + object 4 bytes; PrecisionThread: Thread: _onStart: shared control block (~12+ bytes; allocate_shared may co-locate object) + object 4 bytes; PrecisionThread: Thread: _onPause: shared control block (~12+ bytes; allocate_shared may co-locate object) + object 4 bytes; PrecisionThread: Thread: _onTerminate: shared control block (~12+ bytes; allocate_shared may co-locate object) + object 4 bytes; PrecisionThread: Thread: _onTerminated: shared control block (~12+ bytes; allocate_shared may co-locate object) + object 4 bytes; PrecisionThread: Thread: _onInitializationFailed: shared control block (~12+ bytes; allocate_shared may co-locate object) + object 4 bytes; PrecisionThread: Thread: _onExecutionFailed: shared control block (~12+ bytes; allocate_shared may co-locate object) + object 4 bytes; PrecisionThread: Thread: _onStateChange: shared control block (~12+ bytes; allocate_shared may co-locate object) + object 4 bytes; PrecisionThread: _iterationObservable: shared control block (~12+ bytes; allocate_shared may co-locate object) + object 96 bytes; PrecisionThread: _iterationObservable: pointee: ThreadSafeObservable: Observable: IUntypedObservable: IObservable: enable_shared_from_this: embedded weak_ptr shares a control block when activated; PrecisionThread: _iterationObservable: pointee: ThreadSafeObservable: Observable: IUntypedObservable: IObservable: _lifetimeControl: shared control block (~12+ bytes; allocate_shared may co-locate object) + object 20 bytes; PrecisionThread: _iterationObservable: pointee: ThreadSafeObservable: Observable: IUntypedObservable: IObservable: _lifetimeControl: pointee: _mutex: native synchronization state may allocate platform resources lazily; PrecisionThread: _iterationObservable: pointee: ThreadSafeObservable: Observable: IUntypedObservable: IObservable: _lifetimeControl: pointee: _condition: native condition-variable state may allocate platform synchronization resources; PrecisionThread: _iterationObservable: pointee: ThreadSafeObservable: Observable: _registrations: Capacity * (12 bytes) element storage; PrecisionThread: _iterationObservable: pointee: ThreadSafeObservable: Observable: _bindings: Capacity * (12 bytes) element storage; PrecisionThread: _iterationObservable: pointee: ThreadSafeObservable: _mutex: _owned: owned object: 4 bytes; PrecisionThread: _iterationObservable: pointee: ThreadSafeObservable: _mutex: _fallback: _mutex: native synchronization state may allocate platform resources lazily; PrecisionThread: _iterationObservable: pointee: ThreadSafeObservable: _notificationMutex: _owned: owned object: 4 bytes; PrecisionThread: _iterationObservable: pointee: ThreadSafeObservable: _notificationMutex: _fallback: _mutex: native synchronization state may allocate platform resources lazily; PrecisionThread: _scheduleSignal: owned object: 4 bytes; PrecisionThread: _timingMutex: _owned: owned object: 4 bytes; PrecisionThread: _timingMutex: _fallback: _mutex: native synchronization state may allocate platform resources lazily; PrecisionThread: _iterationSamples: implementation blocks containing N * (8 bytes) plus block-map pointers; _managerHandle: owned object: 4 bytes; _dirtyStates: Capacity * (24 bytes) element storage; _processingStates: Capacity * (24 bytes) element storage; _dirtyAvailability: Capacity * (28 bytes) element storage; _processingAvailability: Capacity * (28 bytes) element storage; _dirtyReachability: Capacity * (18 bytes) element storage; _processingReachability: Capacity * (18 bytes) element storage; _delivered: Capacity * (16 bytes known/aligned storage + sizeof(RemoteStateTuple<TContract>::Type) (target/toolchain dependent)) element storage; _observable: shared control block (~12+ bytes; allocate_shared may co-locate object) + object 96 bytes; _observable: pointee: ThreadSafeObservable: Observable: IUntypedObservable: IObservable: enable_shared_from_this: embedded weak_ptr shares a control block when activated; _observable: pointee: ThreadSafeObservable: Observable: IUntypedObservable: IObservable: _lifetimeControl: shared control block (~12+ bytes; allocate_shared may co-locate object) + object 20 bytes; _observable: pointee: ThreadSafeObservable: Observable: IUntypedObservable: IObservable: _lifetimeControl: pointee: _mutex: native synchronization state may allocate platform resources lazily; _observable: pointee: ThreadSafeObservable: Observable: IUntypedObservable: IObservable: _lifetimeControl: pointee: _condition: native condition-variable state may allocate platform synchronization resources; _observable: pointee: ThreadSafeObservable: Observable: _registrations: Capacity * (12 bytes) element storage; _observable: pointee: ThreadSafeObservable: Observable: _bindings: Capacity * (12 bytes) element storage; _observable: pointee: ThreadSafeObservable: _mutex: _owned: owned object: 4 bytes; _observable: pointee: ThreadSafeObservable: _mutex: _fallback: _mutex: native synchronization state may allocate platform resources lazily; _observable: pointee: ThreadSafeObservable: _notificationMutex: _owned: owned object: 4 bytes; _observable: pointee: ThreadSafeObservable: _notificationMutex: _fallback: _mutex: native synchronization state may allocate platform resources lazily; _dirtyMutex: native synchronization state may allocate platform resources lazily]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * Confidence: medium; compile-time sizeof on the concrete target remains authoritative for ABI-sensitive/opaque members.
 * End ESPressio Memory Audit
 */
template<typename TContract, std::size_t TMaximumDevices, std::size_t TMaximumObservers = 8>
class RemoteStateObserverThread final :
    public Threads::PrecisionThread<Units::NanoSeconds<uint64_t>, Threads::PrecisionThreadTraits<Units::NanoSeconds<uint64_t>>>,
    public IRemoteStateManagerObserver {
    static_assert(TMaximumDevices > 0, "RemoteStateObserverThread device capacity must be non-zero");
    static_assert(TMaximumObservers > 0, "RemoteStateObserverThread observer capacity must be non-zero");

public:
    using Manager = RemoteStateManager<TContract, TMaximumDevices>;
    using Time = Units::NanoSeconds<uint64_t>;
    using Base = Threads::PrecisionThread<Time, Threads::PrecisionThreadTraits<Time>>;
    static constexpr std::size_t MaximumObservers = TMaximumObservers;

private:
    static constexpr std::size_t MaximumDirtyStates = TMaximumDevices * TContract::StateCount;
    static constexpr auto ExternalPreferred = System::Memory::MemoryPolicy::ExternalPreferred;

        /**
     * ESPressio Memory Audit
     * Members:
     * - Device (DeviceIdentifier): 16 bytes [0 bytes dynamic allocation]
     * - TypeId (StateTypeId): 8 bytes [0 bytes dynamic allocation]
     * Total Memory: 24 bytes [0 bytes dynamic allocation]
     * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
     * End ESPressio Memory Audit
     */
struct DirtyState { DeviceIdentifier Device{}; StateTypeId TypeId = 0; };
        /**
     * ESPressio Memory Audit
     * Members:
     * - Address (StateAddress): 24 bytes [0 bytes dynamic allocation]
     * - Previous (StateAvailabilityStatus): 2 bytes [0 bytes dynamic allocation]
     * - Current (StateAvailabilityStatus): 2 bytes [0 bytes dynamic allocation]
     * Total Memory: 28 bytes [0 bytes dynamic allocation]
     * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
     * End ESPressio Memory Audit
     */
struct DirtyAvailability { StateAddress Address{}; StateAvailabilityStatus Previous{}; StateAvailabilityStatus Current{}; };
        /**
     * ESPressio Memory Audit
     * Members:
     * - Device (DeviceIdentifier): 16 bytes [0 bytes dynamic allocation]
     * - Previous (StateSourceReachability): 1 bytes [0 bytes dynamic allocation]
     * - Current (StateSourceReachability): 1 bytes [0 bytes dynamic allocation]
     * Total Memory: 18 bytes [0 bytes dynamic allocation]
     * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
     * End ESPressio Memory Audit
     */
struct DirtyReachability { DeviceIdentifier Device{}; StateSourceReachability Previous = StateSourceReachability::Unknown; StateSourceReachability Current = StateSourceReachability::Unknown; };
        /**
     * ESPressio Memory Audit
     * Members:
     * - Device (DeviceIdentifier): 16 bytes [0 bytes dynamic allocation]
     * - States (RemoteStateTuple<TContract>::Type): sizeof(RemoteStateTuple<TContract>::Type) (target/toolchain dependent) [0 bytes dynamic allocation]
     * Total Memory: 16 bytes known/aligned storage + sizeof(RemoteStateTuple<TContract>::Type) (target/toolchain dependent) [0 bytes dynamic allocation]
     * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
     * Confidence: low; compile-time sizeof on the concrete target remains authoritative for ABI-sensitive/opaque members.
     * End ESPressio Memory Audit
     */
struct DeliveredDevice { DeviceIdentifier Device{}; typename RemoteStateTuple<TContract>::Type States{}; };

    using DirtyStateStorage = System::Memory::Vector<DirtyState, ExternalPreferred>;
    using DirtyAvailabilityStorage = System::Memory::Vector<DirtyAvailability, ExternalPreferred>;
    using DirtyReachabilityStorage = System::Memory::Vector<DirtyReachability, ExternalPreferred>;
    using DeliveredStorage = System::Memory::Vector<DeliveredDevice, ExternalPreferred>;

        /**
     * ESPressio Memory Audit
     * Inherited Memory Total: 96 bytes [ThreadSafeObservable: Observable: IUntypedObservable: IObservable: enable_shared_from_this: embedded weak_ptr shares a control block when activated; ThreadSafeObservable: Observable: IUntypedObservable: IObservable: _lifetimeControl: shared control block (~12+ bytes; allocate_shared may co-locate object) + object 20 bytes; ThreadSafeObservable: Observable: IUntypedObservable: IObservable: _lifetimeControl: pointee: _mutex: native synchronization state may allocate platform resources lazily; ThreadSafeObservable: Observable: IUntypedObservable: IObservable: _lifetimeControl: pointee: _condition: native condition-variable state may allocate platform synchronization resources; ThreadSafeObservable: Observable: _registrations: Capacity * (12 bytes) element storage; ThreadSafeObservable: Observable: _bindings: Capacity * (12 bytes) element storage; ThreadSafeObservable: _mutex: _owned: owned object: 4 bytes; ThreadSafeObservable: _mutex: _fallback: _mutex: native synchronization state may allocate platform resources lazily; ThreadSafeObservable: _notificationMutex: _owned: owned object: 4 bytes; ThreadSafeObservable: _notificationMutex: _fallback: _mutex: native synchronization state may allocate platform resources lazily]
     * Members: none (standalone empty object occupies 1 byte; an eligible empty base may be optimized to 0 bytes).
     * Total Memory: 96 bytes [ThreadSafeObservable: Observable: IUntypedObservable: IObservable: enable_shared_from_this: embedded weak_ptr shares a control block when activated; ThreadSafeObservable: Observable: IUntypedObservable: IObservable: _lifetimeControl: shared control block (~12+ bytes; allocate_shared may co-locate object) + object 20 bytes; ThreadSafeObservable: Observable: IUntypedObservable: IObservable: _lifetimeControl: pointee: _mutex: native synchronization state may allocate platform resources lazily; ThreadSafeObservable: Observable: IUntypedObservable: IObservable: _lifetimeControl: pointee: _condition: native condition-variable state may allocate platform synchronization resources; ThreadSafeObservable: Observable: _registrations: Capacity * (12 bytes) element storage; ThreadSafeObservable: Observable: _bindings: Capacity * (12 bytes) element storage; ThreadSafeObservable: _mutex: _owned: owned object: 4 bytes; ThreadSafeObservable: _mutex: _fallback: _mutex: native synchronization state may allocate platform resources lazily; ThreadSafeObservable: _notificationMutex: _owned: owned object: 4 bytes; ThreadSafeObservable: _notificationMutex: _fallback: _mutex: native synchronization state may allocate platform resources lazily]
     * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
     * End ESPressio Memory Audit
     */
class DispatchObservable final : public Observable::ThreadSafeObservable {
    public:
        template<typename TDefinition>
        void StateChanged(const DeviceIdentifier& device, bool hasPreviousValue, const StateValueType<TDefinition>& previousValue,
                          const StateValueType<TDefinition>& latestValue, StateEpoch epoch, StateRevision revision,
                          StateAvailabilityStatus availability) {
            ExecuteNotification([&](NotificationContext& notification) {
                notification.WithObservers<IRemoteStateObserver<TDefinition>>([&](IRemoteStateObserver<TDefinition>* observer) {
                    observer->OnRemoteStateChanged(StateTag<TDefinition>{}, device, hasPreviousValue, previousValue,
                                                   latestValue, epoch, revision, availability);
                });
            });
        }

        void AvailabilityChanged(const StateAddress& address, StateAvailabilityStatus previous, StateAvailabilityStatus current) {
            ExecuteNotification([&](NotificationContext& notification) {
                notification.WithObservers<IRemoteStateAvailabilityObserver>([&](IRemoteStateAvailabilityObserver* observer) {
                    observer->OnRemoteStateAvailabilityChanged(address, previous, current);
                });
            });
        }

        void ReachabilityChanged(const DeviceIdentifier& device, StateSourceReachability previous, StateSourceReachability current) {
            ExecuteNotification([&](NotificationContext& notification) {
                notification.WithObservers<IRemoteStateReachabilityObserver>([&](IRemoteStateReachabilityObserver* observer) {
                    observer->OnRemoteStateReachabilityChanged(device, previous, current);
                });
            });
        }
    };

    Manager& _manager;
    Observable::ObserverHandlePtr _managerHandle;
    DirtyStateStorage _dirtyStates, _processingStates;
    DirtyAvailabilityStorage _dirtyAvailability, _processingAvailability;
    DirtyReachabilityStorage _dirtyReachability, _processingReachability;
    DeliveredStorage _delivered;
    std::shared_ptr<DispatchObservable> _observable;
    mutable std::mutex _dirtyMutex;
    bool _prepared = false;

    bool EnsureRuntimeStorage() {
        std::lock_guard<std::mutex> lock(_dirtyMutex);
        try {
            if (_dirtyStates.capacity() < MaximumDirtyStates) _dirtyStates.reserve(MaximumDirtyStates);
            if (_processingStates.capacity() < MaximumDirtyStates) _processingStates.reserve(MaximumDirtyStates);
            if (_dirtyAvailability.capacity() < MaximumDirtyStates) _dirtyAvailability.reserve(MaximumDirtyStates);
            if (_processingAvailability.capacity() < MaximumDirtyStates) _processingAvailability.reserve(MaximumDirtyStates);
            if (_dirtyReachability.capacity() < TMaximumDevices) _dirtyReachability.reserve(TMaximumDevices);
            if (_processingReachability.capacity() < TMaximumDevices) _processingReachability.reserve(TMaximumDevices);
            if (_delivered.capacity() < TMaximumDevices) _delivered.reserve(TMaximumDevices);
            if (!_observable) _observable = System::Memory::MakeShared<DispatchObservable, ExternalPreferred>();
            return static_cast<bool>(_observable);
        } catch (...) { return false; }
    }

    template<typename TRegistrar>
    Observable::ObserverHandlePtr RegisterBounded(TRegistrar&& registrar) {
        if (!EnsureRuntimeStorage()) return {};
        std::lock_guard<std::mutex> lock(_dirtyMutex);
        if (_observable->GetObserverCount() >= TMaximumObservers) return {};
        return registrar(*_observable);
    }

    std::shared_ptr<DispatchObservable> ObservableSnapshot() const {
        std::lock_guard<std::mutex> lock(_dirtyMutex);
        return _observable;
    }

    DeliveredDevice* FindOrCreateDelivered(const DeviceIdentifier& device) {
        for (auto& record : _delivered) if (record.Device == device) return &record;
        if (_delivered.size() >= TMaximumDevices) return nullptr;
        _delivered.push_back(DeliveredDevice{});
        _delivered.back().Device = device;
        return &_delivered.back();
    }

    void MarkStateDirty(const DeviceIdentifier& device, StateTypeId typeId) {
        std::lock_guard<std::mutex> lock(_dirtyMutex);
        for (const auto& record : _dirtyStates) if (record.Device == device && record.TypeId == typeId) return;
        if (_dirtyStates.size() < MaximumDirtyStates) _dirtyStates.push_back({device, typeId});
    }

    void MarkAvailabilityDirty(const StateAddress& address, StateAvailabilityStatus previous, StateAvailabilityStatus current) {
        std::lock_guard<std::mutex> lock(_dirtyMutex);
        for (auto& record : _dirtyAvailability) {
            if (record.Address == address) { record.Current = current; return; }
        }
        if (_dirtyAvailability.size() < MaximumDirtyStates) _dirtyAvailability.push_back({address, previous, current});
    }

    void MarkReachabilityDirty(const DeviceIdentifier& device, StateSourceReachability previous, StateSourceReachability current) {
        std::lock_guard<std::mutex> lock(_dirtyMutex);
        for (auto& record : _dirtyReachability) {
            if (record.Device == device) { record.Current = current; return; }
        }
        if (_dirtyReachability.size() < TMaximumDevices) _dirtyReachability.push_back({device, previous, current});
    }

    template<std::size_t TIndex = 0>
    void NotifyState(const DirtyState& dirty) {
        if constexpr (TIndex < TContract::StateCount) {
            using Definition = typename std::tuple_element<TIndex, typename TContract::Definitions>::type;
            using Value = StateValueType<Definition>;
            if (dirty.TypeId == StateTypeIdOf<Definition>) {
                RemoteStateSnapshot<Value> snapshot;
                if (_manager.template Read<Definition>(dirty.Device, snapshot) && snapshot.HasValue) {
                    bool hasPreviousValue = false;
                    Value previousValue{};
                    if (auto* delivered = FindOrCreateDelivered(dirty.Device)) {
                        auto& previous = std::get<TIndex>(delivered->States);
                        hasPreviousValue = previous.HasValue;
                        if (hasPreviousValue) previousValue = previous.Value;
                        auto observable = ObservableSnapshot();
                        if (observable) {
                            observable->template StateChanged<Definition>(dirty.Device, hasPreviousValue, previousValue,
                                snapshot.Value, snapshot.Epoch, snapshot.Revision, snapshot.Availability);
                        }
                        previous.Value = snapshot.Value;
                        previous.Epoch = snapshot.Epoch;
                        previous.Revision = snapshot.Revision;
                        previous.HasValue = true;
                    }
                }
                return;
            }
            NotifyState<TIndex + 1>(dirty);
        }
    }

    template<std::size_t TIndex = 0>
    void MarkAllStatesDirty(const DeviceIdentifier& device) {
        if constexpr (TIndex < TContract::StateCount) {
            using Definition = typename std::tuple_element<TIndex, typename TContract::Definitions>::type;
            MarkStateDirty(device, StateTypeIdOf<Definition>);
            MarkAllStatesDirty<TIndex + 1>(device);
        }
    }

    void Drain() {
        auto observable = ObservableSnapshot();
        if (!observable) return;
        {
            std::lock_guard<std::mutex> lock(_dirtyMutex);
            _processingStates.swap(_dirtyStates);
            _processingAvailability.swap(_dirtyAvailability);
            _processingReachability.swap(_dirtyReachability);
        }
        for (const auto& record : _processingAvailability) observable->AvailabilityChanged(record.Address, record.Previous, record.Current);
        _processingAvailability.clear();
        for (const auto& record : _processingReachability) observable->ReachabilityChanged(record.Device, record.Previous, record.Current);
        _processingReachability.clear();
        for (const auto& record : _processingStates) NotifyState(record);
        _processingStates.clear();
    }

protected:
    void OnWorkWake() override { Drain(); }
    void Iterate(Time, Time, Threads::SkippedIterationCount) override { Drain(); }

public:
    explicit RemoteStateObserverThread(Manager& manager) : Base(), _manager(manager) {
        this->SetStartOnInitialize(false);
        this->SetStackSize(ESPRESSIO_STATE_OBSERVER_THREAD_STACK_SIZE);
        this->SetPriority(ESPRESSIO_STATE_OBSERVER_THREAD_PRIORITY);
        this->SetIterationPeriod(Units::MilliSeconds<uint32_t>(1000));
    }

    bool Prepare() {
        if (_prepared) return true;
        if (!EnsureRuntimeStorage()) return false;
        _managerHandle = _manager.RegisterObserver(static_cast<IRemoteStateManagerObserver*>(this));
        _prepared = static_cast<bool>(_managerHandle);
        return _prepared;
    }

    void ShutdownObserverThread() {
        _managerHandle.reset();
        _prepared = false;
        {
            std::lock_guard<std::mutex> lock(_dirtyMutex);
            _dirtyStates.clear(); _processingStates.clear();
            _dirtyAvailability.clear(); _processingAvailability.clear();
            _dirtyReachability.clear(); _processingReachability.clear();
            _delivered.clear();
        }
        this->Shutdown();
    }

    bool IsPrepared() const noexcept { return _prepared; }

    template<typename TDefinition>
    Observable::ObserverHandlePtr RegisterStateObserver(IRemoteStateObserver<TDefinition>* observer) {
        static_assert(TContract::template Contains<TDefinition>, "State definition is not part of this StateContract");
        if (observer == nullptr) return {};
        return RegisterBounded([&](DispatchObservable& observable) {
            return observable.template RegisterObserverAs<IRemoteStateObserver<TDefinition>>(observer);
        });
    }

    Observable::ObserverHandlePtr RegisterAvailabilityObserver(IRemoteStateAvailabilityObserver* observer) {
        if (observer == nullptr) return {};
        return RegisterBounded([&](DispatchObservable& observable) {
            return observable.template RegisterObserverAs<IRemoteStateAvailabilityObserver>(observer);
        });
    }

    Observable::ObserverHandlePtr RegisterReachabilityObserver(IRemoteStateReachabilityObserver* observer) {
        if (observer == nullptr) return {};
        return RegisterBounded([&](DispatchObservable& observable) {
            return observable.template RegisterObserverAs<IRemoteStateReachabilityObserver>(observer);
        });
    }

    void UnregisterObserver(Observable::IObserver* observer) {
        auto observable = ObservableSnapshot();
        if (observable) observable->UnregisterObserver(observer);
    }

    void OnRemoteStateAccepted(const DeviceIdentifier& device, StateTypeId typeId, StateEpoch, StateRevision, bool changed) override {
        if (!changed || !_prepared) return;
        MarkStateDirty(device, typeId);
        this->WakeForWork();
    }

    void OnRemoteStateAvailabilityChanged(const StateAddress& address, StateAvailabilityStatus previous, StateAvailabilityStatus current) override {
        if (!_prepared) return;
        MarkAvailabilityDirty(address, previous, current);
        MarkStateDirty(address.Device, address.TypeId);
        this->WakeForWork();
    }

    void OnRemoteStateReachabilityChanged(const DeviceIdentifier& device, StateSourceReachability previous, StateSourceReachability current) override {
        if (!_prepared) return;
        MarkReachabilityDirty(device, previous, current);
        MarkAllStatesDirty(device);
        this->WakeForWork();
    }
};

} // namespace State
} // namespace ESPressio
