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
#include "ESPressio_StateAddress.hpp"
#include "ESPressio_StateAvailability.hpp"
#include "ESPressio_StateContract.hpp"
#include "ESPressio_StateObservers.hpp"

namespace ESPressio {
namespace State {

/// <summary>Read-only snapshot of one known remote device and its current transport-independent reachability.</summary>
struct RemoteDeviceSnapshot {
    DeviceIdentifier Identifier{};
    StateSourceReachability Reachability = StateSourceReachability::Unknown;
};

template<typename TValue>
struct RemoteStateSlot {
    TValue Value{};
    StateEpoch Epoch = 0;
    StateRevision Revision = 0;
    StateAvailability AuthoritativeAvailability = StateAvailability::Unavailable;
    StateAvailabilityReason AuthoritativeReason = StateAvailabilityReason::SourceUnbound;
    bool HasValue = false;
};

template<typename TValue>
struct RemoteStateSnapshot {
    TValue Value{};
    StateEpoch Epoch = 0;
    StateRevision Revision = 0;
    StateAvailabilityStatus Availability{};
    StateSourceReachability Reachability = StateSourceReachability::Unknown;
    bool HasValue = false;
};

template<typename TContract>
struct RemoteStateTuple;

template<typename... TDefinitions>
struct RemoteStateTuple<StateContract<TDefinitions...>> {
    using Type = std::tuple<RemoteStateSlot<StateValueType<TDefinitions>>...>;
};

/// <summary>Maintains bounded typed State replicas and source reachability for remote devices.</summary>
/// <typeparam name="TContract">State contract accepted by the manager.</typeparam>
/// <typeparam name="TMaximumDevices">Maximum number of remote devices retained simultaneously.</typeparam>
/// <remarks>
/// Value availability belongs to each State identity and is distinct from device
/// reachability. Read operations return the effective availability obtained by
/// combining the source-authoritative State status with current reachability.
/// </remarks>
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
                notification.WithObservers<IRemoteStateManagerObserver>([&](IRemoteStateManagerObserver* observer) {
                    observer->OnRemoteStateDeviceRegistered(identifier);
                });
            });
        }

        void StateAccepted(const DeviceIdentifier& identifier, StateTypeId typeId, StateEpoch epoch, StateRevision revision, bool changed) {
            ExecuteNotification([&](NotificationContext& notification) {
                notification.WithObservers<IRemoteStateManagerObserver>([&](IRemoteStateManagerObserver* observer) {
                    observer->OnRemoteStateAccepted(identifier, typeId, epoch, revision, changed);
                });
            });
        }

        void StateRejected(const DeviceIdentifier& identifier, StateTypeId typeId, StateEpoch epoch, StateRevision revision) {
            ExecuteNotification([&](NotificationContext& notification) {
                notification.WithObservers<IRemoteStateManagerObserver>([&](IRemoteStateManagerObserver* observer) {
                    observer->OnRemoteStateRejected(identifier, typeId, epoch, revision);
                });
            });
        }

        void ReachabilityChanged(const DeviceIdentifier& identifier, StateSourceReachability previous, StateSourceReachability current) {
            ExecuteNotification([&](NotificationContext& notification) {
                notification.WithObservers<IRemoteStateManagerObserver>([&](IRemoteStateManagerObserver* observer) {
                    observer->OnRemoteStateReachabilityChanged(identifier, previous, current);
                });
            });
        }

        void StateAvailabilityChanged(const StateAddress& address, StateAvailabilityStatus previous, StateAvailabilityStatus current) {
            ExecuteNotification([&](NotificationContext& notification) {
                notification.WithObservers<IRemoteStateManagerObserver>([&](IRemoteStateManagerObserver* observer) {
                    observer->OnRemoteStateAvailabilityChanged(address, previous, current);
                });
            });
        }
    };

    struct DeviceRecord {
        DeviceIdentifier Identifier{};
        StateSourceReachability Reachability = StateSourceReachability::Unknown;
        typename RemoteStateTuple<TContract>::Type States{};
    };

    using DeviceStorage = System::Memory::Vector<DeviceRecord, System::Memory::MemoryPolicy::ExternalPreferred>;
    using SnapshotStorage = System::Memory::Vector<RemoteDeviceSnapshot, System::Memory::MemoryPolicy::ExternalPreferred>;

    mutable DeviceStorage _devices;
    mutable System::Synchronization::RecursiveMutex _mutex;
    mutable std::shared_ptr<ManagerObservable> _observable;

    bool EnsureRuntimeStorage() const {
        std::lock_guard<System::Synchronization::RecursiveMutex> lock(_mutex);
        try {
            if (_devices.capacity() < TMaximumDevices) _devices.reserve(TMaximumDevices);
            if (!_observable) {
                _observable = System::Memory::MakeShared<ManagerObservable, System::Memory::MemoryPolicy::ExternalPreferred>();
            }
            return static_cast<bool>(_observable);
        } catch (...) {
            return false;
        }
    }

    DeviceRecord* FindLocked(const DeviceIdentifier& identifier) {
        for (auto& device : _devices) if (device.Identifier == identifier) return &device;
        return nullptr;
    }

    const DeviceRecord* FindLocked(const DeviceIdentifier& identifier) const {
        for (const auto& device : _devices) if (device.Identifier == identifier) return &device;
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

    template<typename TDefinition>
    static StateAvailabilityStatus EffectiveAvailability(const DeviceRecord& device) {
        const auto& slot = std::get<TContract::template IndexOf<TDefinition>()>(device.States);
        return ResolveEffectiveStateAvailability(slot.AuthoritativeAvailability, slot.AuthoritativeReason, device.Reachability);
    }

    template<typename TDefinition, typename TValue>
    bool ApplyValue(const DeviceIdentifier& identifier, StateEpoch epoch, StateRevision revision, TValue&& value) {
        static_assert(TContract::template Contains<TDefinition>, "State definition is not part of this StateContract");
        if (!EnsureRuntimeStorage()) return false;

        bool created = false;
        bool changed = false;
        bool accepted = false;
        StateAvailabilityStatus previousAvailability{};
        StateAvailabilityStatus currentAvailability{};
        {
            std::lock_guard<System::Synchronization::RecursiveMutex> lock(_mutex);
            auto* device = FindOrCreateLocked(identifier, created);
            if (device != nullptr && revision != 0) {
                auto& slot = std::get<TContract::template IndexOf<TDefinition>()>(device->States);
                previousAvailability = EffectiveAvailability<TDefinition>(*device);
                if (!slot.HasValue || epoch > slot.Epoch || (epoch == slot.Epoch && revision > slot.Revision)) {
                    changed = !slot.HasValue || !(slot.Value == value);
                    slot.Value = std::forward<TValue>(value);
                    slot.Epoch = epoch;
                    slot.Revision = revision;
                    slot.HasValue = true;
                    slot.AuthoritativeAvailability = StateAvailability::Available;
                    slot.AuthoritativeReason = StateAvailabilityReason::None;
                    currentAvailability = EffectiveAvailability<TDefinition>(*device);
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
        if (previousAvailability != currentAvailability) {
            _observable->StateAvailabilityChanged(MakeStateAddress<TDefinition>(identifier), previousAvailability, currentAvailability);
        }
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
    bool Read(const DeviceIdentifier& identifier, RemoteStateSnapshot<StateValueType<TDefinition>>& snapshot) const {
        static_assert(TContract::template Contains<TDefinition>, "State definition is not part of this StateContract");
        if (!EnsureRuntimeStorage()) return false;
        std::lock_guard<System::Synchronization::RecursiveMutex> lock(_mutex);
        const auto* device = FindLocked(identifier);
        if (device == nullptr) return false;
        const auto& slot = std::get<TContract::template IndexOf<TDefinition>()>(device->States);
        snapshot.Value = slot.Value;
        snapshot.Epoch = slot.Epoch;
        snapshot.Revision = slot.Revision;
        snapshot.HasValue = slot.HasValue;
        snapshot.Reachability = device->Reachability;
        snapshot.Availability = EffectiveAvailability<TDefinition>(*device);
        return true;
    }

    template<typename TDefinition>
    bool Apply(const DeviceIdentifier& identifier, StateEpoch epoch, StateRevision revision, const StateValueType<TDefinition>& value) {
        return ApplyValue<TDefinition>(identifier, epoch, revision, value);
    }

    template<typename TDefinition>
    bool Apply(const DeviceIdentifier& identifier, StateEpoch epoch, StateRevision revision, StateValueType<TDefinition>&& value) {
        return ApplyValue<TDefinition>(identifier, epoch, revision, std::move(value));
    }

    template<typename TDefinition>
    bool ApplyAvailability(const DeviceIdentifier& identifier, StateAvailability availability, StateAvailabilityReason reason = StateAvailabilityReason::None) {
        static_assert(TContract::template Contains<TDefinition>, "State definition is not part of this StateContract");
        if (!EnsureRuntimeStorage()) return false;
        bool created = false;
        StateAvailabilityStatus previous{};
        StateAvailabilityStatus current{};
        {
            std::lock_guard<System::Synchronization::RecursiveMutex> lock(_mutex);
            auto* device = FindOrCreateLocked(identifier, created);
            if (device == nullptr) return false;
            auto& slot = std::get<TContract::template IndexOf<TDefinition>()>(device->States);
            previous = EffectiveAvailability<TDefinition>(*device);
            slot.AuthoritativeAvailability = availability;
            slot.AuthoritativeReason = reason;
            current = EffectiveAvailability<TDefinition>(*device);
        }
        if (created) _observable->DeviceRegistered(identifier);
        if (previous != current) _observable->StateAvailabilityChanged(MakeStateAddress<TDefinition>(identifier), previous, current);
        return true;
    }

    bool SetReachability(const DeviceIdentifier& identifier, StateSourceReachability reachability) {
        if (!EnsureRuntimeStorage()) return false;
        bool created = false;
        StateSourceReachability previous = StateSourceReachability::Unknown;
        bool changed = false;
        {
            std::lock_guard<System::Synchronization::RecursiveMutex> lock(_mutex);
            auto* device = FindOrCreateLocked(identifier, created);
            if (device == nullptr) return false;
            previous = device->Reachability;
            changed = previous != reachability;
            device->Reachability = reachability;
        }
        if (created) _observable->DeviceRegistered(identifier);
        if (changed) _observable->ReachabilityChanged(identifier, previous, reachability);
        return true;
    }

    StateSourceReachability GetReachability(const DeviceIdentifier& identifier) const {
        if (!EnsureRuntimeStorage()) return StateSourceReachability::Unknown;
        std::lock_guard<System::Synchronization::RecursiveMutex> lock(_mutex);
        const auto* device = FindLocked(identifier);
        return device != nullptr ? device->Reachability : StateSourceReachability::Unknown;
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
            for (const auto& device : _devices) snapshots.push_back(RemoteDeviceSnapshot{device.Identifier, device.Reachability});
        }
        for (const auto& snapshot : snapshots) callback(snapshot);
    }
};

} // namespace State
} // namespace ESPressio
