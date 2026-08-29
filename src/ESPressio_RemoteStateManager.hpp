#pragma once

#include <cstddef>
#include <memory>
#include <mutex>
#include <tuple>
#include <type_traits>
#include <utility>

#include <ESPressio_Memory.hpp>
#include <ESPressio_ThreadSafeObservable.hpp>

#include "ESPressio_DeviceIdentifier.hpp"
#include "ESPressio_StateContract.hpp"
#include "ESPressio_StateObservers.hpp"

namespace ESPressio {
namespace State {

/// <summary>Describes the currently known connectivity or freshness state of a remote device.</summary>
enum class RemoteDeviceAvailability : uint8_t {
    Unknown = 0,
    Connected,
    Stale,
    Disconnected,
    ConnectionLost
};

/// <summary>Lightweight snapshot of a known remote device and its availability.</summary>
struct RemoteDeviceSnapshot {
    /// <summary>Stable remote-device identifier.</summary>
    DeviceIdentifier Identifier{};
    /// <summary>Current known device availability.</summary>
    RemoteDeviceAvailability Availability = RemoteDeviceAvailability::Unknown;
};

/// <summary>Internal typed storage for the latest accepted value and revision of one remote state.</summary>
template<typename TValue>
struct RemoteStateSlot {
    TValue Value{};
    StateEpoch Epoch = 0;
    StateRevision Revision = 0;
    bool HasValue = false;
};

/// <summary>Read-only snapshot of one typed remote state and its owning device's availability.</summary>
template<typename TValue>
struct RemoteStateSnapshot {
    /// <summary>Latest accepted state value.</summary>
    TValue Value{};
    /// <summary>Epoch associated with the latest accepted value.</summary>
    StateEpoch Epoch = 0;
    /// <summary>Revision associated with the latest accepted value.</summary>
    StateRevision Revision = 0;
    /// <summary>Current known availability of the owning remote device.</summary>
    RemoteDeviceAvailability Availability = RemoteDeviceAvailability::Unknown;
    /// <summary>Indicates whether a value has yet been accepted for this state.</summary>
    bool HasValue = false;
};

/// <summary>Maps a state contract to its tuple of typed remote-state slots.</summary>
template<typename TContract>
struct RemoteStateTuple;

template<typename... TDefinitions>
struct RemoteStateTuple<StateContract<TDefinitions...>> {
    /// <summary>Tuple containing one typed remote-state slot per contract definition.</summary>
    using Type = std::tuple<RemoteStateSlot<StateValueType<TDefinitions>>...>;
};

/// <summary>Maintains bounded typed state snapshots and availability for remote devices.</summary>
/// <typeparam name="TContract">State contract accepted by the manager.</typeparam>
/// <typeparam name="TMaximumDevices">Maximum number of remote devices retained simultaneously.</typeparam>
/// <remarks>Construction is allocation-free; bounded device storage and observer infrastructure are materialized lazily on first use so globally constructed managers can bind ExternalPreferred storage to the installed platform provider.</remarks>
template<typename TContract, std::size_t TMaximumDevices>
class RemoteStateManager final {
public:
    /// <summary>State contract accepted by this manager.</summary>
    using Contract = TContract;
    /// <summary>Maximum number of remote-device records retained by this manager.</summary>
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
        bool Used = false;
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
    mutable std::recursive_mutex _mutex;
    mutable std::shared_ptr<ManagerObservable> _observable;

    /// <summary>Materializes bounded runtime storage using the provider active at first actual use.</summary>
    bool EnsureRuntimeStorage() const {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        try {
            if (_devices.size() != TMaximumDevices) {
                _devices.assign(TMaximumDevices, DeviceRecord{});
            }
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
            if (device.Used && device.Identifier == identifier) return &device;
        }
        return nullptr;
    }

    const DeviceRecord* FindLocked(const DeviceIdentifier& identifier) const {
        for (const auto& device : _devices) {
            if (device.Used && device.Identifier == identifier) return &device;
        }
        return nullptr;
    }

    DeviceRecord* FindOrCreateLocked(const DeviceIdentifier& identifier, bool& created) {
        created = false;
        if (identifier.IsZero()) return nullptr;
        if (auto* existing = FindLocked(identifier)) return existing;
        for (auto& device : _devices) {
            if (!device.Used) {
                device.Used = true;
                device.Identifier = identifier;
                created = true;
                return &device;
            }
        }
        return nullptr;
    }

    template<typename TDefinition, typename TValue>
    bool ApplyValue(
        const DeviceIdentifier& identifier,
        StateEpoch epoch,
        StateRevision revision,
        TValue&& value
    ) {
        static_assert(TContract::template Contains<TDefinition>,
            "State definition is not part of this StateContract");
        if (!EnsureRuntimeStorage()) return false;

        bool created = false;
        bool changed = false;
        bool accepted = false;
        {
            std::lock_guard<std::recursive_mutex> lock(_mutex);
            auto* device = FindOrCreateLocked(identifier, created);
            if (device == nullptr || revision == 0) {
                accepted = false;
            } else {
                auto& slot = std::get<TContract::template IndexOf<TDefinition>()>(device->States);
                if (slot.HasValue &&
                    (epoch < slot.Epoch || (epoch == slot.Epoch && revision <= slot.Revision))) {
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
            _observable->StateRejected(identifier, StateTypeIdOf<TDefinition>, epoch, revision);
            return false;
        }
        _observable->StateAccepted(identifier, StateTypeIdOf<TDefinition>, epoch, revision, changed);
        return true;
    }

public:
    /// <summary>Creates an empty bounded remote-state manager without allocating its runtime tables.</summary>
    RemoteStateManager() = default;

    /// <summary>Registers an observer for manager-level remote-state and availability events.</summary>
    Observable::ObserverHandlePtr RegisterObserver(IRemoteStateManagerObserver* observer) {
        if (!EnsureRuntimeStorage()) return {};
        return _observable->template RegisterObserverAs<IRemoteStateManagerObserver>(observer);
    }

    /// <summary>Unregisters a previously registered manager observer.</summary>
    void UnregisterObserver(Observable::IObserver* observer) {
        if (!EnsureRuntimeStorage()) return;
        _observable->UnregisterObserver(observer);
    }

    /// <summary>Reads the latest snapshot for one typed state on a remote device.</summary>
    /// <typeparam name="TDefinition">State definition to read.</typeparam>
    /// <returns><c>true</c> when the remote device is known.</returns>
    template<typename TDefinition>
    bool Read(
        const DeviceIdentifier& identifier,
        RemoteStateSnapshot<StateValueType<TDefinition>>& snapshot
    ) const {
        static_assert(TContract::template Contains<TDefinition>,
            "State definition is not part of this StateContract");
        if (!EnsureRuntimeStorage()) return false;
        std::lock_guard<std::recursive_mutex> lock(_mutex);
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

    /// <summary>Applies a copied remote-state value when its epoch/revision is newer than the retained value.</summary>
    template<typename TDefinition>
    bool Apply(
        const DeviceIdentifier& identifier,
        StateEpoch epoch,
        StateRevision revision,
        const StateValueType<TDefinition>& value
    ) {
        return ApplyValue<TDefinition>(identifier, epoch, revision, value);
    }

    /// <summary>Applies a moved remote-state value when its epoch/revision is newer than the retained value.</summary>
    template<typename TDefinition>
    bool Apply(
        const DeviceIdentifier& identifier,
        StateEpoch epoch,
        StateRevision revision,
        StateValueType<TDefinition>&& value
    ) {
        return ApplyValue<TDefinition>(identifier, epoch, revision, std::move(value));
    }

    /// <summary>Sets the known availability of a remote device, creating its record when capacity permits.</summary>
    bool SetAvailability(
        const DeviceIdentifier& identifier,
        RemoteDeviceAvailability availability
    ) {
        if (!EnsureRuntimeStorage()) return false;
        bool created = false;
        RemoteDeviceAvailability previous = RemoteDeviceAvailability::Unknown;
        bool changed = false;
        {
            std::lock_guard<std::recursive_mutex> lock(_mutex);
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

    /// <summary>Gets the known availability of a remote device.</summary>
    RemoteDeviceAvailability GetAvailability(const DeviceIdentifier& identifier) const {
        if (!EnsureRuntimeStorage()) return RemoteDeviceAvailability::Unknown;
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        const auto* device = FindLocked(identifier);
        return device != nullptr ? device->Availability : RemoteDeviceAvailability::Unknown;
    }

    /// <summary>Gets the number of currently retained remote-device records.</summary>
    std::size_t GetDeviceCount() const {
        if (!EnsureRuntimeStorage()) return 0;
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        std::size_t count = 0;
        for (const auto& device : _devices) if (device.Used) ++count;
        return count;
    }

    /// <summary>Invokes a callback for a stable externally backed snapshot of each known remote device.</summary>
    /// <typeparam name="TCallback">Callable accepting a <c>RemoteDeviceSnapshot</c>.</typeparam>
    template<typename TCallback>
    void ForEachDevice(TCallback&& callback) const {
        if (!EnsureRuntimeStorage()) return;
        SnapshotStorage snapshots;
        snapshots.reserve(TMaximumDevices);
        {
            std::lock_guard<std::recursive_mutex> lock(_mutex);
            for (const auto& device : _devices) {
                if (!device.Used) continue;
                snapshots.push_back(RemoteDeviceSnapshot{device.Identifier, device.Availability});
            }
        }
        for (const auto& snapshot : snapshots) callback(snapshot);
    }
};

} // namespace State
} // namespace ESPressio
