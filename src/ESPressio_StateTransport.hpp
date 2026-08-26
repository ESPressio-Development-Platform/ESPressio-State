#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>

#include <ESPressio_ThreadSafeObservable.hpp>

#include "ESPressio_DeviceIdentifier.hpp"
#include "ESPressio_StateContract.hpp"
#include "ESPressio_StateObservers.hpp"

namespace ESPressio {
namespace State {

struct StateUpdateHeader {
    DeviceIdentifier Origin{};
    StateTypeId TypeId = 0;
    StateEpoch Epoch = 0;
    StateRevision Revision = 0;
};

struct StateAcknowledgement {
    DeviceIdentifier Origin{};
    StateTypeId TypeId = 0;
    StateEpoch Epoch = 0;
    StateRevision Revision = 0;
};

template<typename TValue>
struct StateUpdate final {
    StateUpdateHeader Header{};
    TValue Value{};
};

template<typename TValue>
struct PendingStateUpdate final {
    bool Pending = false;
    StateEpoch Epoch = 0;
    StateRevision Revision = 0;
    StateRevision LastAcknowledgedRevision = 0;
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
    bool Replace(
        StateEpoch epoch,
        StateRevision revision,
        const TValue& value
    ) {
        return ReplaceValue(epoch, revision, value);
    }

    bool Replace(
        StateEpoch epoch,
        StateRevision revision,
        TValue&& value
    ) {
        return ReplaceValue(epoch, revision, std::move(value));
    }

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

template<typename TDefinition>
class StatePublicationTracker final {
public:
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
    std::shared_ptr<PublicationObservable> _observable =
        std::make_shared<PublicationObservable>();

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
    explicit StatePublicationTracker(
        const DeviceIdentifier& destination = DeviceIdentifier{}
    ) : _destination(destination) {}

    Observable::ObserverHandlePtr RegisterObserver(
        IStatePublicationObserver* observer
    ) {
        return _observable->template RegisterObserverAs<
            IStatePublicationObserver
        >(observer);
    }

    void UnregisterObserver(IStatePublicationObserver* observer) {
        _observable->UnregisterObserver(observer);
    }

    const DeviceIdentifier& Destination() const noexcept {
        return _destination;
    }

    void SetDestination(const DeviceIdentifier& destination) {
        _destination = destination;
    }

    const PendingStateUpdate<Value>& PendingUpdate() const noexcept {
        return _pending;
    }

    bool Replace(
        StateEpoch epoch,
        StateRevision revision,
        const Value& value
    ) {
        return ReplaceValue(epoch, revision, value);
    }

    bool Replace(
        StateEpoch epoch,
        StateRevision revision,
        Value&& value
    ) {
        return ReplaceValue(epoch, revision, std::move(value));
    }

    bool Acknowledge(StateEpoch epoch, StateRevision revision) {
        const bool current =
            _pending.Pending &&
            epoch == _pending.Epoch &&
            revision >= _pending.Revision;

        const bool accepted = _pending.Acknowledge(epoch, revision);
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
