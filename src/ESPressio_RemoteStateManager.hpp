#pragma once

#include <array>
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
    bool HasAvailability = false;
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
/// <typeparam name="TContract">Closed set of State definitions accepted by the manager.</typeparam>
/// <typeparam name="TMaximumDevices">Maximum number of remote devices retained simultaneously.</typeparam>
/// <typeparam name="TMaximumObservers">Maximum simultaneous lifecycle observer registrations.</typeparam>
/// <remarks>
/// Authoritative State availability and device reachability are stored separately.
/// Every read and availability notification exposes their effective combination.
/// Device storage and observer registration are independently bounded.
/// </remarks>
template<typename TContract, std::size_t TMaximumDevices, std::size_t TMaximumObservers = 8>
class RemoteStateManager final {
    static_assert(TMaximumDevices > 0, "RemoteStateManager device capacity must be non-zero");
    static_assert(TMaximumObservers > 0, "RemoteStateManager observer capacity must be non-zero");

public:
    using Contract = TContract;
    static constexpr std::size_t MaximumDevices = TMaximumDevices;
    static constexpr std::size_t MaximumObservers = TMaximumObservers;

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

    struct AvailabilityTransition {
        StateAddress Address{};
        StateAvailabilityStatus Previous{};
        StateAvailabilityStatus Current{};
        bool Active = false;
    };

    using DeviceStorage = System::Memory::Vector<DeviceRecord, System::Memory::MemoryPolicy::ExternalPreferred>;
    using SnapshotStorage = System::Memory::Vector<RemoteDeviceSnapshot, System::Memory::MemoryPolicy::ExternalPreferred>;
    using TransitionStorage = std::array<AvailabilityTransition, TContract::StateCount>;

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
        } catch (...) { return false; }
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

    template<std::size_t TIndex = 0>
    static void CaptureReachabilityTransitions(
        const DeviceRecord& device,
        StateSourceReachability previousReachability,
        StateSourceReachability currentReachability,
        TransitionStorage& transitions
    ) {
        if constexpr (TIndex < TContract::StateCount) {
            using Definition = typename std::tuple_element<TIndex, typename TContract::Definitions>::type;
            const auto& slot = std::get<TIndex>(device.States);
            if (slot.HasValue || slot.HasAvailability) {
                const auto previous = ResolveEffectiveStateAvailability(
                    slot.AuthoritativeAvailability,
                    slot.AuthoritativeReason,
                    previousReachability
                );
                const auto current = ResolveEffectiveStateAvailability(
                    slot.AuthoritativeAvailability,
                    slot.AuthoritativeReason,
                    currentReachability
                );
                if (previous != current) {
                    transitions[TIndex] = {
                        MakeStateAddress<Definition>(device.Identifier),
                        previous,
                        current,
                        true
                    };
                }
            }
            CaptureReachabilityTransitions<TIndex + 1>(device, previousReachability, currentReachability, transitions);
        }
    }

    template<typename TDefinition, typename TValue>
    bool ApplyValue(const DeviceIdentifier& identifier, StateEpoch epoch, StateRevision revision, TValue&& value) {
        static_assert(TContract::template Contains<TDefinition>, "State definition is not part of this StateContract");
        if (!EnsureRuntimeStorage()) return false;
        bool created = false, changed = false, accepted = false;
        StateAvailabilityStatus previousAvailability{}, currentAvailability{};
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
                    slot.HasAvailability = true;
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

    /// <summary>Registers a lifecycle observer when the manager's bounded observer capacity permits.</summary>
    Observable::ObserverHandlePtr RegisterObserver(IRemoteStateManagerObserver* observer) {
        if (observer == nullptr || !EnsureRuntimeStorage()) return {};
        std::lock_guard<System::Synchronization::RecursiveMutex> lock(_mutex);
        if (_observable->GetObserverCount() >= TMaximumObservers) return {};
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
        StateAvailabilityStatus previous{}, current{};
        {
            std::lock_guard<System::Synchronization::RecursiveMutex> lock(_mutex);
            auto* device = FindOrCreateLocked(identifier, created);
            if (device == nullptr) return false;
            auto& slot = std::get<TContract::template IndexOf<TDefinition>()>(device->States);
            previous = EffectiveAvailability<TDefinition>(*device);
            slot.AuthoritativeAvailability = availability;
            slot.AuthoritativeReason = reason;
            slot.HasAvailability = true;
            current = EffectiveAvailability<TDefinition>(*device);
        }
        if (created) _observable->DeviceRegistered(identifier);
        if (previous != current) _observable->StateAvailabilityChanged(MakeStateAddress<TDefinition>(identifier), previous, current);
        return true;
    }

    bool SetReachability(const DeviceIdentifier& identifier, StateSourceReachability reachability) {
        if (!EnsureRuntimeStorage()) return false;
        bool created = false, changed = false;
        StateSourceReachability previous = StateSourceReachability::Unknown;
        TransitionStorage transitions{};
        {
            std::lock_guard<System::Synchronization::RecursiveMutex> lock(_mutex);
            auto* device = FindOrCreateLocked(identifier, created);
            if (device == nullptr) return false;
            previous = device->Reachability;
            changed = previous != reachability;
            if (changed) {
                CaptureReachabilityTransitions(*device, previous, reachability, transitions);
                device->Reachability = reachability;
            }
        }
        if (created) _observable->DeviceRegistered(identifier);
        if (changed) {
            _observable->ReachabilityChanged(identifier, previous, reachability);
            for (const auto& transition : transitions) {
                if (transition.Active) {
                    _observable->StateAvailabilityChanged(transition.Address, transition.Previous, transition.Current);
                }
            }
        }
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
