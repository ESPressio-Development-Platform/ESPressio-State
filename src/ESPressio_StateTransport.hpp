#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>

#include <ESPressio_Memory.hpp>
#include <ESPressio_ThreadSafeObservable.hpp>

#include "ESPressio_DeviceIdentifier.hpp"
#include "ESPressio_StateContract.hpp"
#include "ESPressio_StateObservers.hpp"

namespace ESPressio {
namespace State {

/// <summary>Transport metadata that uniquely identifies a published state revision.</summary>
struct StateUpdateHeader {
    /// <summary>Device that originated the state update.</summary>
    DeviceIdentifier Origin{};
    /// <summary>Stable state type identifier.</summary>
    StateTypeId TypeId = 0;
    /// <summary>State epoch associated with the update.</summary>
    StateEpoch Epoch = 0;
    /// <summary>State revision associated with the update.</summary>
    StateRevision Revision = 0;
};

/// <summary>Identifies a state revision acknowledged by a remote endpoint.</summary>
struct StateAcknowledgement {
    /// <summary>Origin device whose publication is being acknowledged.</summary>
    DeviceIdentifier Origin{};
    /// <summary>Stable state type identifier.</summary>
    StateTypeId TypeId = 0;
    /// <summary>State epoch being acknowledged.</summary>
    StateEpoch Epoch = 0;
    /// <summary>State revision being acknowledged.</summary>
    StateRevision Revision = 0;
};

/// <summary>Combines state transport metadata with a strongly typed value.</summary>
/// <typeparam name="TValue">State value type.</typeparam>
template<typename TValue>
struct StateUpdate final {
    /// <summary>Transport metadata for the update.</summary>
    StateUpdateHeader Header{};
    /// <summary>Published state value.</summary>
    TValue Value{};
};

/// <summary>Tracks the latest unacknowledged value for one state publication target.</summary>
/// <typeparam name="TValue">Published state value type.</typeparam>
template<typename TValue>
struct PendingStateUpdate final {
    /// <summary>Indicates whether an update is currently awaiting acknowledgement.</summary>
    bool Pending = false;
    /// <summary>Epoch of the latest retained update.</summary>
    StateEpoch Epoch = 0;
    /// <summary>Revision of the latest retained update.</summary>
    StateRevision Revision = 0;
    /// <summary>Highest revision accepted as acknowledged for the current epoch.</summary>
    StateRevision LastAcknowledgedRevision = 0;
    /// <summary>Latest retained state value.</summary>
    TValue Value{};

private:
    template<typename TValueArgument>
    bool ReplaceValue(
        StateEpoch epoch,
        StateRevision revision,
        TValueArgument&& value
    ) {
        if (revision == 0) return false;
        if (Pending && epoch < Epoch) return false;
        if (Pending && epoch == Epoch && revision <= Revision) return false;

        Epoch = epoch;
        Revision = revision;
        Value = std::forward<TValueArgument>(value);
        Pending = true;
        return true;
    }

public:
    /// <summary>Replaces the retained pending value when the supplied epoch/revision is newer.</summary>
    bool Replace(
        StateEpoch epoch,
        StateRevision revision,
        const TValue& value
    ) {
        return ReplaceValue(epoch, revision, value);
    }

    /// <summary>Moves a replacement value into the pending update when the supplied epoch/revision is newer.</summary>
    bool Replace(
        StateEpoch epoch,
        StateRevision revision,
        TValue&& value
    ) {
        return ReplaceValue(epoch, revision, std::move(value));
    }

    /// <summary>Records acknowledgement of a revision in the current epoch and clears pending state when satisfied.</summary>
    /// <returns><c>true</c> when the acknowledgement is valid for the tracked epoch and revision history.</returns>
    bool Acknowledge(StateEpoch epoch, StateRevision revision) {
        if (epoch != Epoch || revision == 0) return false;
        if (revision < LastAcknowledgedRevision) return false;

        LastAcknowledgedRevision = revision;
        if (Pending && revision >= Revision) {
            Pending = false;
        }
        return true;
    }
};

/// <summary>Tracks publication and acknowledgement lifecycle for one typed state and destination device.</summary>
/// <typeparam name="TDefinition">State definition being published.</typeparam>
/// <remarks>Observer infrastructure is optional and allocation-free unless a lifecycle observer is actually registered.</remarks>
template<typename TDefinition>
class StatePublicationTracker final {
public:
    /// <summary>Value type represented by the tracked state definition.</summary>
    using Value = StateValueType<TDefinition>;

private:
    class PublicationObservable final : public Observable::ThreadSafeObservable {
    public:
        void Pending(
            const DeviceIdentifier& destination,
            StateEpoch epoch,
            StateRevision revision
        ) {
            ExecuteNotification([&](NotificationContext& notification) {
                notification.WithObservers<IStatePublicationObserver>(
                    [&](IStatePublicationObserver* observer) {
                        observer->OnStatePublicationPending(
                            destination,
                            StateTypeIdOf<TDefinition>,
                            epoch,
                            revision
                        );
                    }
                );
            });
        }

        void Superseded(
            const DeviceIdentifier& destination,
            StateEpoch previousEpoch,
            StateRevision previousRevision,
            StateEpoch epoch,
            StateRevision revision
        ) {
            ExecuteNotification([&](NotificationContext& notification) {
                notification.WithObservers<IStatePublicationObserver>(
                    [&](IStatePublicationObserver* observer) {
                        observer->OnStatePublicationSuperseded(
                            destination,
                            StateTypeIdOf<TDefinition>,
                            previousEpoch,
                            previousRevision,
                            epoch,
                            revision
                        );
                    }
                );
            });
        }

        void Acknowledged(
            const DeviceIdentifier& destination,
            StateEpoch epoch,
            StateRevision revision
        ) {
            ExecuteNotification([&](NotificationContext& notification) {
                notification.WithObservers<IStatePublicationObserver>(
                    [&](IStatePublicationObserver* observer) {
                        observer->OnStatePublicationAcknowledged(
                            destination,
                            StateTypeIdOf<TDefinition>,
                            epoch,
                            revision
                        );
                    }
                );
            });
        }

        void StaleAcknowledgement(
            const DeviceIdentifier& destination,
            StateEpoch epoch,
            StateRevision revision
        ) {
            ExecuteNotification([&](NotificationContext& notification) {
                notification.WithObservers<IStatePublicationObserver>(
                    [&](IStatePublicationObserver* observer) {
                        observer->OnStatePublicationStaleAcknowledgement(
                            destination,
                            StateTypeIdOf<TDefinition>,
                            epoch,
                            revision
                        );
                    }
                );
            });
        }
    };

    DeviceIdentifier _destination{};
    PendingStateUpdate<Value> _pending{};
    std::shared_ptr<PublicationObservable> _observable;

    bool EnsureObservable() {
        if (_observable) return true;
        try {
            _observable = System::Memory::MakeShared<
                PublicationObservable,
                System::Memory::MemoryPolicy::ExternalPreferred
            >();
            return static_cast<bool>(_observable);
        } catch (...) {
            return false;
        }
    }

    template<typename TValueArgument>
    bool ReplaceValue(
        StateEpoch epoch,
        StateRevision revision,
        TValueArgument&& value
    ) {
        const bool hadPending = _pending.Pending;
        const StateEpoch previousEpoch = _pending.Epoch;
        const StateRevision previousRevision = _pending.Revision;

        if (!
            _pending.Replace(
                epoch,
                revision,
                std::forward<TValueArgument>(value)
            )
        ) {
            return false;
        }

        // The tracker has no mandatory side-channel work. If no lifecycle
        // observer was ever registered, avoid creating notification
        // infrastructure merely to discover that there is nobody to notify.
        if (!_observable) return true;

        if (hadPending) {
            _observable->Superseded(
                _destination,
                previousEpoch,
                previousRevision,
                epoch,
                revision
            );
        } else {
            _observable->Pending(_destination, epoch, revision);
        }
        return true;
    }

public:
    /// <summary>Creates an allocation-free publication tracker for an optional destination device.</summary>
    explicit StatePublicationTracker(
        const DeviceIdentifier& destination = DeviceIdentifier{}
    ) : _destination(destination) {}

    /// <summary>Registers an observer for publication lifecycle notifications, materializing ExternalPreferred observer storage on demand.</summary>
    Observable::ObserverHandlePtr RegisterObserver(
        IStatePublicationObserver* observer
    ) {
        if (!EnsureObservable()) return {};
        return _observable->template RegisterObserverAs<
            IStatePublicationObserver
        >(observer);
    }

    /// <summary>Unregisters a publication lifecycle observer.</summary>
    void UnregisterObserver(IStatePublicationObserver* observer) {
        if (_observable) _observable->UnregisterObserver(observer);
    }

    /// <summary>Gets the destination associated with this tracker.</summary>
    const DeviceIdentifier& Destination() const noexcept {
        return _destination;
    }

    /// <summary>Changes the destination associated with subsequent publication notifications.</summary>
    void SetDestination(const DeviceIdentifier& destination) {
        _destination = destination;
    }

    /// <summary>Gets the current pending-update state.</summary>
    const PendingStateUpdate<Value>& PendingUpdate() const noexcept {
        return _pending;
    }

    /// <summary>Replaces the pending publication with a newer value revision.</summary>
    bool Replace(
        StateEpoch epoch,
        StateRevision revision,
        const Value& value
    ) {
        return ReplaceValue(epoch, revision, value);
    }

    /// <summary>Moves a newer value revision into the pending publication.</summary>
    bool Replace(
        StateEpoch epoch,
        StateRevision revision,
        Value&& value
    ) {
        return ReplaceValue(epoch, revision, std::move(value));
    }

    /// <summary>Processes an acknowledgement and emits current or stale acknowledgement notifications only when observers exist.</summary>
    bool Acknowledge(StateEpoch epoch, StateRevision revision) {
        const bool current =
            _pending.Pending &&
            epoch == _pending.Epoch &&
            revision >= _pending.Revision;

        const bool accepted = _pending.Acknowledge(epoch, revision);
        if (!_observable) return accepted;

        if (!accepted || !current) {
            _observable->StaleAcknowledgement(
                _destination,
                epoch,
                revision
            );
            return accepted;
        }

        _observable->Acknowledged(
            _destination,
            epoch,
            revision
        );
        return true;
    }
};

}
}
