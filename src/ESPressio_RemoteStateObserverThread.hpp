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
/// <remarks>Manager callbacks only mark bounded dirty-state sets and wake the worker; observer callbacks are dispatched later from the thread context. Construction is allocation-free; bounded ExternalPreferred bookkeeping capacity is reserved during <c>Prepare()</c> or first observer registration and only live entries are materialized.</remarks>
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
        DeviceIdentifier Device{};
        StateTypeId TypeId = 0;
    };

    struct DirtyAvailability {
        DeviceIdentifier Device{};
        RemoteDeviceAvailability Previous = RemoteDeviceAvailability::Unknown;
        RemoteDeviceAvailability Current = RemoteDeviceAvailability::Unknown;
    };

    struct DeliveredDevice {
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

    // Dirty sets remain double-buffered so producer callbacks never contend
    // with observer delivery. Only capacity is reserved up-front: live entries
    // are appended sparsely, avoiding two fully materialized state tables and
    // two fully materialized availability tables for mostly-idle systems.
    DirtyStateStorage _dirtyStates;
    DirtyStateStorage _processingStates;
    DirtyAvailabilityStorage _dirtyAvailability;
    DirtyAvailabilityStorage _processingAvailability;
    DeliveredStorage _delivered;

    std::shared_ptr<DispatchObservable> _observable;
    mutable std::mutex _dirtyMutex;
    bool _prepared = false;

    /// <summary>Reserves bounded observer bookkeeping with the active System memory provider.</summary>
    bool EnsureRuntimeStorage() {
        std::lock_guard<std::mutex> lock(_dirtyMutex);
        try {
            if (_dirtyStates.capacity() < MaximumDirtyStates) {
                _dirtyStates.reserve(MaximumDirtyStates);
            }
            if (_processingStates.capacity() < MaximumDirtyStates) {
                _processingStates.reserve(MaximumDirtyStates);
            }
            if (_dirtyAvailability.capacity() < TMaximumDevices) {
                _dirtyAvailability.reserve(TMaximumDevices);
            }
            if (_processingAvailability.capacity() < TMaximumDevices) {
                _processingAvailability.reserve(TMaximumDevices);
            }
            if (_delivered.capacity() < TMaximumDevices) {
                _delivered.reserve(TMaximumDevices);
            }
            if (!_observable) {
                _observable = System::Memory::MakeShared<
                    DispatchObservable,
                    ExternalPreferred
                >();
            }
            return static_cast<bool>(_observable);
        } catch (...) {
            return false;
        }
    }

    DeliveredDevice* FindOrCreateDelivered(const DeviceIdentifier& device) {
        for (auto& record : _delivered) {
            if (record.Device == device) return &record;
        }
        if (_delivered.size() >= TMaximumDevices) return nullptr;
        _delivered.push_back(DeliveredDevice{});
        _delivered.back().Device = device;
        return &_delivered.back();
    }

    void MarkStateDirty(const DeviceIdentifier& device, StateTypeId typeId) {
        std::lock_guard<std::mutex> lock(_dirtyMutex);
        for (const auto& record : _dirtyStates) {
            if (record.Device == device && record.TypeId == typeId) return;
        }
        if (_dirtyStates.size() >= MaximumDirtyStates) return;
        _dirtyStates.push_back(DirtyState{device, typeId});
    }

    void MarkAvailabilityDirty(
        const DeviceIdentifier& device,
        RemoteDeviceAvailability previous,
        RemoteDeviceAvailability current
    ) {
        std::lock_guard<std::mutex> lock(_dirtyMutex);
        for (auto& record : _dirtyAvailability) {
            if (record.Device == device) {
                record.Current = current;
                return;
            }
        }
        if (_dirtyAvailability.size() >= TMaximumDevices) return;
        _dirtyAvailability.push_back(DirtyAvailability{
            device,
            previous,
            current
        });
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
        if (!_observable) return;
        {
            std::lock_guard<std::mutex> lock(_dirtyMutex);
            _processingStates.swap(_dirtyStates);
            _processingAvailability.swap(_dirtyAvailability);
        }

        for (const auto& record : _processingAvailability) {
            _observable->AvailabilityChanged(
                record.Device,
                record.Previous,
                record.Current
            );
        }
        _processingAvailability.clear();

        for (const auto& record : _processingStates) {
            NotifyState(record);
        }
        _processingStates.clear();
    }

protected:
    /// <summary>Drains pending remote-state notifications when the worker is explicitly woken.</summary>
    void OnWorkWake() override { Drain(); }
    /// <summary>Drains pending remote-state notifications during the periodic precision-thread iteration.</summary>
    void Iterate(Time, Time, Threads::SkippedIterationCount) override { Drain(); }

public:
    /// <summary>Creates an allocation-free observer thread bound to the supplied remote-state manager.</summary>
    explicit RemoteStateObserverThread(Manager& manager)
        : Base(), _manager(manager) {
        this->SetStartOnInitialize(false);
        this->SetStackSize(ESPRESSIO_STATE_OBSERVER_THREAD_STACK_SIZE);
        this->SetPriority(ESPRESSIO_STATE_OBSERVER_THREAD_PRIORITY);
        this->SetIterationPeriod(Units::MilliSeconds<uint32_t>(1000));
    }

    /// <summary>Reserves bounded bookkeeping and registers the thread with its remote-state manager before the worker is started.</summary>
    /// <returns><c>true</c> when storage and manager observer registration are active.</returns>
    bool Prepare() {
        if (_prepared) return true;
        if (!EnsureRuntimeStorage()) return false;
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
            _dirtyStates.clear();
            _processingStates.clear();
            _dirtyAvailability.clear();
            _processingAvailability.clear();
            _delivered.clear();
        }
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
        if (!EnsureRuntimeStorage()) return {};
        return _observable->template RegisterObserverAs<
            IRemoteStateObserver<TDefinition>
        >(observer);
    }

    /// <summary>Registers an observer for asynchronously delivered remote-device availability changes.</summary>
    Observable::ObserverHandlePtr RegisterAvailabilityObserver(
        IRemoteDeviceAvailabilityObserver* observer
    ) {
        if (!EnsureRuntimeStorage()) return {};
        return _observable->template RegisterObserverAs<
            IRemoteDeviceAvailabilityObserver
        >(observer);
    }

    /// <summary>Unregisters a previously registered dispatch observer.</summary>
    void UnregisterObserver(Observable::IObserver* observer) {
        if (_observable) _observable->UnregisterObserver(observer);
    }

    /// <summary>Marks a meaningfully changed accepted state for deferred thread-context delivery.</summary>
    void OnRemoteStateAccepted(
        const DeviceIdentifier& device,
        StateTypeId typeId,
        StateEpoch,
        StateRevision,
        bool changed
    ) override {
        if (!changed || !_prepared) return;
        MarkStateDirty(device, typeId);
        this->WakeForWork();
    }

    /// <summary>Marks an availability transition and the device's state set for deferred thread-context delivery.</summary>
    void OnRemoteStateAvailabilityChanged(
        const DeviceIdentifier& device,
        RemoteDeviceAvailability previous,
        RemoteDeviceAvailability current
    ) override {
        if (!_prepared) return;
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
