#pragma once

#include <cstddef>
#include <memory>
#include <mutex>
#include <tuple>
#include <type_traits>
#include <utility>

#include <ESPressio_Memory.hpp>
#include <ESPressio_Synchronization.hpp>
#include <ESPressio_ThreadSafeObservable.hpp>

#include "ESPressio_DeviceIdentifier.hpp"
#include "ESPressio_StateContract.hpp"
#include "ESPressio_StateObservers.hpp"

namespace ESPressio {
namespace State {

enum class RemoteDeviceAvailability : uint8_t {
    Unknown = 0,
    Connected,
    Stale,
    Disconnected,
    ConnectionLost
};

struct RemoteDeviceSnapshot {
    DeviceIdentifier Identifier{};
    RemoteDeviceAvailability Availability = RemoteDeviceAvailability::Unknown;
};

template<typename TValue>
struct RemoteStateSlot {
    TValue Value{};
    StateEpoch Epoch = 0;
    StateRevision Revision = 0;
    bool HasValue = false;
};

template<typename TValue>
struct RemoteStateSnapshot {
    TValue Value{};
    StateEpoch Epoch = 0;
    StateRevision Revision = 0;
    RemoteDeviceAvailability Availability = RemoteDeviceAvailability::Unknown;
    bool HasValue = false;
};

template<typename TContract>
struct RemoteStateTuple;

template<typename... TDefinitions>
struct RemoteStateTuple<StateContract<TDefinitions...>> {
    using Type = std::tuple<RemoteStateSlot<StateValueType<TDefinitions>>...>;
};

/// <summary>Maintains bounded typed state snapshots and availability for remote devices.</summary>
/// <typeparam name="TContract">State contract accepted by the manager.</typeparam>
/// <typeparam name="TMaximumDevices">Maximum number of remote devices retained simultaneously.</typeparam>
/// <remarks>Runtime mutation is serialized through ESPressio System synchronization. Storage is reserved lazily in ExternalPreferred memory and records are materialized only for observed devices.</remarks>
template<typename TContract, std::size_t TMaximumDevices>
class RemoteStateManager final {
public:
    using Contract = TContract;
    static constexpr std::size_t MaximumDevices = TMaximumDevices;

private:
    class ManagerObservable final : public Observable::ThreadSafeObservable {
    public:
        void DeviceRegistered(const DeviceIdentifier& identifier) {
            ExecuteNotification([&](NotificationContext& notification) {
                notification.WithObservers<IRemoteStateManagerObserver>(
                    [&](IRemoteStateManagerObserver* observer) {
                        observer->OnRemoteStateDeviceRegistered(identifier);
                    }
                );
            });
        }

        void StateAccepted(
            const DeviceIdentifier& identifier,
            StateTypeId typeId,
            StateEpoch epoch,
            StateRevision revision,
            bool changed
        ) {
            ExecuteNotification([&](NotificationContext& notification) {
                notification.WithObservers<IRemoteStateManagerObserver>(
                    [&](IRemoteStateManagerObserver* observer) {
                        observer->OnRemoteStateAccepted(identifier, typeId, epoch, revision, changed);
                    }
                );
            });
        }

        void StateRejected(
            const DeviceIdentifier& identifier,
            StateTypeId typeId,
            StateEpoch epoch,
            StateRevision revision
        ) {
            ExecuteNotification([&](NotificationContext& notification) {
                notification.WithObservers<IRemoteStateManagerObserver>(
                    [&](IRemoteStateManagerObserver* observer) {
                        observer->OnRemoteStateRejected(identifier, typeId, epoch, revision);
                    }
                );
            });
        }

        void AvailabilityChanged(
            const DeviceIdentifier& identifier,
            RemoteDeviceAvailability previous,
            RemoteDeviceAvailability current
        ) {
            ExecuteNotification([&](NotificationContext& notification) {
                notification.WithObservers<IRemoteStateManagerObserver>(
                    [&](IRemoteStateManagerObserver* observer) {
                        observer->OnRemoteStateAvailabilityChanged(identifier, previous, current);
                    }
                );
            });
        }
    };

    struct DeviceRecord {
        DeviceIdentifier Identifier{};
        RemoteDeviceAvailability Availability = RemoteDeviceAvailability::Unknown;
        typename RemoteStateTuple<TContract>::Type States{};
    };

    using DeviceStorage = System::Memory::Vector<
        DeviceRecord,
        System::Memory::MemoryPolicy::ExternalPreferred
    >;
    using SnapshotStorage = System::Memory::Vector<
        RemoteDeviceSnapshot,
        System::Memory::MemoryPolicy::ExternalPreferred
    >;

    mutable DeviceStorage _devices;
    mutable System::Synchronization::RecursiveMutex _mutex;
    mutable std::shared_ptr<ManagerObservable> _observable;

    bool EnsureRuntimeStorage() const {
        std::lock_guard<System::Synchronization::RecursiveMutex> lock(_mutex);
        try {
            if (_devices.capacity() < TMaximumDevices) _devices.reserve(TMaximumDevices);
            if (!_observable) {
                _observable = System::Memory::MakeShared<
                    ManagerObservable,
                    System::Memory::MemoryPolicy::ExternalPreferred
                >();
            }
            return static_cast<bool>(_observable);
        } catch (...) {
            return false;
        }
    }

    DeviceRecord* FindLocked(const DeviceIdentifier& identifier) {
        for (auto& device : _devices) {
            if (device.Identifier == identifier) return &device;
        }
        return nullptr;
    }

    const DeviceRecord* FindLocked(const DeviceIdentifier& identifier) const {
        for (const auto& device : _devices) {
            if (device.Identifier == identifier) return &device;
        }
        return nullptr;
    }

    DeviceRecord* FindOrCreateLocked(const DeviceIdentifier& identifier, bool& created) {
        created = false;
        if (identifier.IsZero()) return nullptr;
        if (auto* existing = FindLocked(identifier)) return existing;
        if (_devices.size() >= TMaximumDevices) return nullptr;
        _devices.push_back(DeviceRecord{});
        _devices.back().Identifier = identifier;
        created = true;
        return &_devices.back();
    }

    template<typename TDefinition, typename TValue>
    bool ApplyValue(
        const DeviceIdentifier& identifier,
        StateEpoch epoch,
        StateRevision revision,
        TValue&& value
    ) {
        static_assert(
            TContract::template Contains<TDefinition>,
            "State definition is not part of this StateContract"
        );
        if (!EnsureRuntimeStorage()) return false;

        bool created = false;
        bool changed = false;
        bool accepted = false;
        {
            std::lock_guard<System::Synchronization::RecursiveMutex> lock(_mutex);
            auto* device = FindOrCreateLocked(identifier, created);
            if (device == nullptr || revision == 0) {
                accepted = false;
            } else {
                auto& slot = std::get<TContract::template IndexOf<TDefinition>()>(device->States);
                if (
                    slot.HasValue &&
                    (epoch < slot.Epoch || (epoch == slot.Epoch && revision <= slot.Revision))
                ) {
                    accepted = false;
                } else {
                    changed = !slot.HasValue || !(slot.Value == value);
                    slot.Value = std::forward<TValue>(value);
                    slot.Epoch = epoch;
                    slot.Revision = revision;
                    slot.HasValue = true;
                    accepted = true;
                }
            }
        }

        if (created) _observable->DeviceRegistered(identifier);
        if (!accepted) {
            _observable->StateRejected(
                identifier,
                StateTypeIdOf<TDefinition>,
                epoch,
                revision
            );
            return false;
        }
        _observable->StateAccepted(
            identifier,
            StateTypeIdOf<TDefinition>,
            epoch,
            revision,
            changed
        );
        return true;
    }

public:
    RemoteStateManager() = default;

    Observable::ObserverHandlePtr RegisterObserver(IRemoteStateManagerObserver* observer) {
        if (!EnsureRuntimeStorage()) return {};
        return _observable->template RegisterObserverAs<IRemoteStateManagerObserver>(observer);
    }

    void UnregisterObserver(Observable::IObserver* observer) {
        if (!EnsureRuntimeStorage()) return;
        _observable->UnregisterObserver(observer);
    }

    template<typename TDefinition>
    bool Read(
        const DeviceIdentifier& identifier,
        RemoteStateSnapshot<StateValueType<TDefinition>>& snapshot
    ) const {
        static_assert(
            TContract::template Contains<TDefinition>,
            "State definition is not part of this StateContract"
        );
        if (!EnsureRuntimeStorage()) return false;
        std::lock_guard<System::Synchronization::RecursiveMutex> lock(_mutex);
        const auto* device = FindLocked(identifier);
        if (device == nullptr) return false;
        const auto& slot = std::get<TContract::template IndexOf<TDefinition>()>(device->States);
        snapshot.Value = slot.Value;
        snapshot.Epoch = slot.Epoch;
        snapshot.Revision = slot.Revision;
        snapshot.HasValue = slot.HasValue;
        snapshot.Availability = device->Availability;
        return true;
    }

    template<typename TDefinition>
    bool Apply(
        const DeviceIdentifier& identifier,
        StateEpoch epoch,
        StateRevision revision,
        const StateValueType<TDefinition>& value
    ) {
        return ApplyValue<TDefinition>(identifier, epoch, revision, value);
    }

    template<typename TDefinition>
    bool Apply(
        const DeviceIdentifier& identifier,
        StateEpoch epoch,
        StateRevision revision,
        StateValueType<TDefinition>&& value
    ) {
        return ApplyValue<TDefinition>(identifier, epoch, revision, std::move(value));
    }

    bool SetAvailability(
        const DeviceIdentifier& identifier,
        RemoteDeviceAvailability availability
    ) {
        if (!EnsureRuntimeStorage()) return false;
        bool created = false;
        RemoteDeviceAvailability previous = RemoteDeviceAvailability::Unknown;
        bool changed = false;
        {
            std::lock_guard<System::Synchronization::RecursiveMutex> lock(_mutex);
            auto* device = FindOrCreateLocked(identifier, created);
            if (device == nullptr) return false;
            previous = device->Availability;
            changed = previous != availability;
            device->Availability = availability;
        }
        if (created) _observable->DeviceRegistered(identifier);
        if (changed) _observable->AvailabilityChanged(identifier, previous, availability);
        return true;
    }

    RemoteDeviceAvailability GetAvailability(const DeviceIdentifier& identifier) const {
        if (!EnsureRuntimeStorage()) return RemoteDeviceAvailability::Unknown;
        std::lock_guard<System::Synchronization::RecursiveMutex> lock(_mutex);
        const auto* device = FindLocked(identifier);
        return device != nullptr ? device->Availability : RemoteDeviceAvailability::Unknown;
    }

    std::size_t GetDeviceCount() const {
        if (!EnsureRuntimeStorage()) return 0;
        std::lock_guard<System::Synchronization::RecursiveMutex> lock(_mutex);
        return _devices.size();
    }

    template<typename TCallback>
    void ForEachDevice(TCallback&& callback) const {
        if (!EnsureRuntimeStorage()) return;
        SnapshotStorage snapshots;
        {
            std::lock_guard<System::Synchronization::RecursiveMutex> lock(_mutex);
            snapshots.reserve(_devices.size());
            for (const auto& device : _devices) {
                snapshots.push_back(RemoteDeviceSnapshot{
                    device.Identifier,
                    device.Availability
                });
            }
        }
        for (const auto& snapshot : snapshots) callback(snapshot);
    }
};

} // namespace State
} // namespace ESPressio
