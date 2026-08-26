#pragma once

#if !__has_include(<ESPressio_PrecisionThread.hpp>)
#error "RemoteStateObserverThread requires ESPressio Threads. Include the Threads working branch when using this optional execution layer."
#endif

#include <array>
#include <cstddef>
#include <memory>
#include <mutex>
#include <tuple>

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

template<typename TContract, std::size_t TMaximumDevices>
class RemoteStateObserverThread final :
    public Threads::PrecisionThread<
        Units::NanoSeconds<uint64_t>,
        Threads::PrecisionThreadTraits<Units::NanoSeconds<uint64_t>>
    >,
    public IRemoteStateManagerObserver {
public:
    using Manager = RemoteStateManager<TContract, TMaximumDevices>;
    using Time = Units::NanoSeconds<uint64_t>;
    using Base = Threads::PrecisionThread<
        Time,
        Threads::PrecisionThreadTraits<Time>
    >;

private:
    static constexpr std::size_t MaximumDirtyStates =
        TMaximumDevices * TContract::StateCount;

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
    std::array<DirtyState, MaximumDirtyStates> _dirtyStates{};
    std::array<DirtyAvailability, TMaximumDevices> _dirtyAvailability{};
    std::array<DeliveredDevice, TMaximumDevices> _delivered{};
    std::shared_ptr<DispatchObservable> _observable =
        std::make_shared<DispatchObservable>();
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
        std::array<DirtyState, MaximumDirtyStates> dirtyStates{};
        std::array<DirtyAvailability, TMaximumDevices> dirtyAvailability{};
        {
            std::lock_guard<std::mutex> lock(_dirtyMutex);
            dirtyStates = _dirtyStates;
            dirtyAvailability = _dirtyAvailability;
            _dirtyStates = {};
            _dirtyAvailability = {};
        }
        for (const auto& record : dirtyAvailability) {
            if (!record.Used) continue;
            _observable->AvailabilityChanged(record.Device, record.Previous, record.Current);
        }
        for (const auto& record : dirtyStates) {
            if (!record.Used) continue;
            NotifyState(record);
        }
    }

protected:
    void OnWorkWake() override { Drain(); }
    void Iterate(Time, Time, Threads::SkippedIterationCount) override { Drain(); }

public:
    explicit RemoteStateObserverThread(Manager& manager)
        : Base(), _manager(manager) {
        this->SetStartOnInitialize(false);
        this->SetStackSize(ESPRESSIO_STATE_OBSERVER_THREAD_STACK_SIZE);
        this->SetPriority(ESPRESSIO_STATE_OBSERVER_THREAD_PRIORITY);
        this->SetIterationPeriod(Units::MilliSeconds<uint32_t>(1000));
    }

    bool Prepare() {
        if (_prepared) return true;
        _managerHandle = _manager.RegisterObserver(
            static_cast<IRemoteStateManagerObserver*>(this)
        );
        _prepared = static_cast<bool>(_managerHandle);
        return _prepared;
    }

    void ShutdownObserverThread() {
        _managerHandle.reset();
        _prepared = false;
        _delivered = {};
        this->Shutdown();
    }

    bool IsPrepared() const noexcept { return _prepared; }

    Observable::ObserverHandlePtr RegisterObserver(Observable::IObserver* observer) {
        return _observable->RegisterObserver(observer);
    }

    void UnregisterObserver(Observable::IObserver* observer) {
        _observable->UnregisterObserver(observer);
    }

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

}
}
