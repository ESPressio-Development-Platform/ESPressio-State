#pragma once

#if !__has_include(<ESPressio_PrecisionThread.hpp>)
#error "RemoteStateObserverThread requires ESPressio Threads. Include the Threads working branch when using this optional execution layer."
#endif

#include <algorithm>
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

/// <summary>Moves remote-state change notifications onto a dedicated precision-thread execution context.</summary>
/// <typeparam name="TContract">State contract observed by the thread.</typeparam>
/// <typeparam name="TMaximumDevices">Maximum remote-device capacity of the associated manager.</typeparam>
/// <remarks>Manager callbacks only mark bounded dirty-state sets and wake the worker; observer callbacks are dispatched later from the thread context.</remarks>
template<typename TContract, std::size_t TMaximumDevices>
class RemoteStateObserverThread final :
    public Threads::PrecisionThread<
        Units::NanoSeconds<uint64_t>,
        Threads::PrecisionThreadTraits<Units::NanoSeconds<uint64_t>>
    >,
    public IRemoteStateManagerObserver {
public:
    /// <summary>Remote-state manager type consumed by this observer thread.</summary>
    using Manager = RemoteStateManager<TContract, TMaximumDevices>;
    /// <summary>Time-unit type used by the precision-thread base.</summary>
    using Time = Units::NanoSeconds<uint64_t>;
    /// <summary>Precision-thread base type.</summary>
    using Base = Threads::PrecisionThread<
        Time,
        Threads::PrecisionThreadTraits<Time>
    >;

private:
    static constexpr std::size_t MaximumDirtyStates =
        TMaximumDevices * TContract::StateCount;
    static constexpr auto ExternalPreferred =
        System::Memory::MemoryPolicy::ExternalPreferred;

    struct DirtyState {
        bool Used = false;
        DeviceIdentifier Device{};
        StateTypeId TypeId = 0;
    };

    struct DirtyAvailability {
        bool Used = false;
        DeviceIdentifier Device{};
        RemoteDeviceAvailability Previous = RemoteDeviceAvailability::Unknown;
        RemoteDeviceAvailability Current = RemoteDeviceAvailability::Unknown;
    };

    struct DeliveredDevice {
        bool Used = false;
        DeviceIdentifier Device{};
        typename RemoteStateTuple<TContract>::Type States{};
    };

    using DirtyStateStorage = System::Memory::Vector<DirtyState, ExternalPreferred>;
    using DirtyAvailabilityStorage = System::Memory::Vector<DirtyAvailability, ExternalPreferred>;
    using DeliveredStorage = System::Memory::Vector<DeliveredDevice, ExternalPreferred>;

    class DispatchObservable final : public Observable::ThreadSafeObservable {
    public:
        template<typename TDefinition>
        void StateChanged(
            const DeviceIdentifier& device,
            bool hasPreviousValue,
            const StateValueType<TDefinition>& previousValue,
            const StateValueType<TDefinition>& latestValue,
            StateEpoch epoch,
            StateRevision revision,
            RemoteDeviceAvailability availability
        ) {
            ExecuteNotification([&](NotificationContext& notification) {
                notification.WithObservers<IRemoteStateObserver<TDefinition>>(
                    [&](IRemoteStateObserver<TDefinition>* observer) {
                        observer->OnRemoteStateChanged(
                            StateTag<TDefinition>{},
                            device,
                            hasPreviousValue,
                            previousValue,
                            latestValue,
                            epoch,
                            revision,
                            availability
                        );
                    }
                );
            });
        }

        void AvailabilityChanged(
            const DeviceIdentifier& device,
            RemoteDeviceAvailability previous,
            RemoteDeviceAvailability current
        ) {
            ExecuteNotification([&](NotificationContext& notification) {
                notification.WithObservers<IRemoteDeviceAvailabilityObserver>(
                    [&](IRemoteDeviceAvailabilityObserver* observer) {
                        observer->OnRemoteDeviceAvailabilityChanged(
                            device,
                            previous,
                            current
                        );
                    }
                );
            });
        }
    };

    Manager& _manager;
    Observable::ObserverHandlePtr _managerHandle;

    // Double-buffered dirty sets are fixed-size after construction. Their
    // backing storage is external-preferred so bounded observer bookkeeping
    // does not permanently consume scarce internal DRAM, while drains remain
    // allocation-free after construction.
    DirtyStateStorage _dirtyStates;
    DirtyStateStorage _processingStates;
    DirtyAvailabilityStorage _dirtyAvailability;
    DirtyAvailabilityStorage _processingAvailability;
    DeliveredStorage _delivered;

    std::shared_ptr<DispatchObservable> _observable;
    std::mutex _dirtyMutex;
    bool _prepared = false;

    DeliveredDevice* FindOrCreateDelivered(const DeviceIdentifier& device) {
        for (auto& record : _delivered) {
            if (record.Used && record.Device == device) return &record;
        }
        for (auto& record : _delivered) {
            if (!record.Used) {
                record.Used = true;
                record.Device = device;
                return &record;
            }
        }
        return nullptr;
    }

    void MarkStateDirty(const DeviceIdentifier& device, StateTypeId typeId) {
        std::lock_guard<std::mutex> lock(_dirtyMutex);
        for (const auto& record : _dirtyStates) {
            if (record.Used && record.Device == device && record.TypeId == typeId) {
                return;
            }
        }
        for (auto& record : _dirtyStates) {
            if (!record.Used) {
                record.Used = true;
                record.Device = device;
                record.TypeId = typeId;
                return;
            }
        }
    }

    void MarkAvailabilityDirty(
        const DeviceIdentifier& device,
        RemoteDeviceAvailability previous,
        RemoteDeviceAvailability current
    ) {
        std::lock_guard<std::mutex> lock(_dirtyMutex);
        for (auto& record : _dirtyAvailability) {
            if (record.Used && record.Device == device) {
                record.Current = current;
                return;
            }
        }
        for (auto& record : _dirtyAvailability) {
            if (!record.Used) {
                record.Used = true;
                record.Device = device;
                record.Previous = previous;
                record.Current = current;
                return;
            }
        }
    }

    template<std::size_t TIndex = 0>
    void NotifyState(const DirtyState& dirty) {
        if constexpr (TIndex < TContract::StateCount) {
            using Definition = typename std::tuple_element<
                TIndex,
                typename TContract::Definitions
            >::type;
            using Value = StateValueType<Definition>;

            if (dirty.TypeId == StateTypeIdOf<Definition>) {
                RemoteStateSnapshot<Value> snapshot;
                if (
                    _manager.template Read<Definition>(dirty.Device, snapshot) &&
                    snapshot.HasValue
                ) {
                    bool hasPreviousValue = false;
                    Value previousValue{};
                    if (auto* delivered = FindOrCreateDelivered(dirty.Device)) {
                        auto& previous = std::get<TIndex>(delivered->States);
                        hasPreviousValue = previous.HasValue;
                        if (hasPreviousValue) previousValue = previous.Value;

                        _observable->template StateChanged<Definition>(
                            dirty.Device,
                            hasPreviousValue,
                            previousValue,
                            snapshot.Value,
                            snapshot.Epoch,
                            snapshot.Revision,
                            snapshot.Availability
                        );

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

    void Drain() {
        {
            std::lock_guard<std::mutex> lock(_dirtyMutex);
            _processingStates.swap(_dirtyStates);
            _processingAvailability.swap(_dirtyAvailability);
        }

        for (auto& record : _processingAvailability) {
            if (!record.Used) continue;
            _observable->AvailabilityChanged(
                record.Device,
                record.Previous,
                record.Current
            );
            record = DirtyAvailability{};
        }

        for (auto& record : _processingStates) {
            if (!record.Used) continue;
            NotifyState(record);
            record = DirtyState{};
        }
    }

protected:
    /// <summary>Drains pending remote-state notifications when the worker is explicitly woken.</summary>
    void OnWorkWake() override { Drain(); }
    /// <summary>Drains pending remote-state notifications during the periodic precision-thread iteration.</summary>
    void Iterate(Time, Time, Threads::SkippedIterationCount) override { Drain(); }

public:
    /// <summary>Creates an observer thread bound to the supplied remote-state manager.</summary>
    explicit RemoteStateObserverThread(Manager& manager)
        : Base(),
          _manager(manager),
          _dirtyStates(MaximumDirtyStates),
          _processingStates(MaximumDirtyStates),
          _dirtyAvailability(TMaximumDevices),
          _processingAvailability(TMaximumDevices),
          _delivered(TMaximumDevices),
          _observable(System::Memory::MakeShared<DispatchObservable, ExternalPreferred>()) {
        this->SetStartOnInitialize(false);
        this->SetStackSize(ESPRESSIO_STATE_OBSERVER_THREAD_STACK_SIZE);
        this->SetPriority(ESPRESSIO_STATE_OBSERVER_THREAD_PRIORITY);
        this->SetIterationPeriod(Units::MilliSeconds<uint32_t>(1000));
    }

    /// <summary>Registers the thread with its remote-state manager before the worker is started.</summary>
    /// <returns><c>true</c> when the manager observer registration is active.</returns>
    bool Prepare() {
        if (_prepared) return true;
        _managerHandle = _manager.RegisterObserver(
            static_cast<IRemoteStateManagerObserver*>(this)
        );
        _prepared = static_cast<bool>(_managerHandle);
        return _prepared;
    }

    /// <summary>Detaches from the manager, clears pending delivery state, and shuts down the worker thread.</summary>
    void ShutdownObserverThread() {
        _managerHandle.reset();
        _prepared = false;
        {
            std::lock_guard<std::mutex> lock(_dirtyMutex);
            std::fill(_dirtyStates.begin(), _dirtyStates.end(), DirtyState{});
            std::fill(_processingStates.begin(), _processingStates.end(), DirtyState{});
            std::fill(_dirtyAvailability.begin(), _dirtyAvailability.end(), DirtyAvailability{});
            std::fill(_processingAvailability.begin(), _processingAvailability.end(), DirtyAvailability{});
        }
        std::fill(_delivered.begin(), _delivered.end(), DeliveredDevice{});
        this->Shutdown();
    }

    /// <summary>Indicates whether the manager observer registration has been prepared.</summary>
    bool IsPrepared() const noexcept { return _prepared; }

    /// <summary>Registers an observer for asynchronously delivered changes to one typed remote state.</summary>
    template<typename TDefinition>
    Observable::ObserverHandlePtr RegisterStateObserver(
        IRemoteStateObserver<TDefinition>* observer
    ) {
        static_assert(
            TContract::template Contains<TDefinition>,
            "State definition is not part of this StateContract"
        );
        return _observable->template RegisterObserverAs<
            IRemoteStateObserver<TDefinition>
        >(observer);
    }

    /// <summary>Registers an observer for asynchronously delivered remote-device availability changes.</summary>
    Observable::ObserverHandlePtr RegisterAvailabilityObserver(
        IRemoteDeviceAvailabilityObserver* observer
    ) {
        return _observable->template RegisterObserverAs<
            IRemoteDeviceAvailabilityObserver
        >(observer);
    }

    /// <summary>Unregisters a previously registered dispatch observer.</summary>
    void UnregisterObserver(Observable::IObserver* observer) {
        _observable->UnregisterObserver(observer);
    }

    /// <summary>Marks a meaningfully changed accepted state for deferred thread-context delivery.</summary>
    void OnRemoteStateAccepted(
        const DeviceIdentifier& device,
        StateTypeId typeId,
        StateEpoch,
        StateRevision,
        bool changed
    ) override {
        if (!changed) return;
        MarkStateDirty(device, typeId);
        this->WakeForWork();
    }

    /// <summary>Marks an availability transition and the device's state set for deferred thread-context delivery.</summary>
    void OnRemoteStateAvailabilityChanged(
        const DeviceIdentifier& device,
        RemoteDeviceAvailability previous,
        RemoteDeviceAvailability current
    ) override {
        MarkAvailabilityDirty(device, previous, current);
        MarkAllStatesDirty(device);
        this->WakeForWork();
    }

private:
    template<std::size_t TIndex = 0>
    void MarkAllStatesDirty(const DeviceIdentifier& device) {
        if constexpr (TIndex < TContract::StateCount) {
            using Definition = typename std::tuple_element<
                TIndex,
                typename TContract::Definitions
            >::type;
            MarkStateDirty(device, StateTypeIdOf<Definition>);
            MarkAllStatesDirty<TIndex + 1>(device);
        }
    }
};

} // namespace State
} // namespace ESPressio
