#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <tuple>
#include <utility>

#include <ESPressio_ThreadSafeObservable.hpp>

#include "ESPressio_DeviceIdentifier.hpp"
#include "ESPressio_StateContract.hpp"
#include "ESPressio_StateObservers.hpp"
#include "ESPressio_StateTransport.hpp"

namespace ESPressio {
namespace State {

/// <summary>Stores the callable source and current revision for one typed published state.</summary>
template<typename TDefinition>
struct StateSourceSlot {
    /// <summary>Value type represented by the state definition.</summary>
    using Value = StateValueType<TDefinition>;
    /// <summary>Callable used to obtain the current state value.</summary>
    std::function<Value()> Source;
    /// <summary>Latest publication revision allocated for this state.</summary>
    StateRevision Revision = 0;
};

/// <summary>Maps a state contract to its tuple of typed publisher source slots.</summary>
template<typename TContract>
struct StateSourceTuple;

template<typename... TDefinitions>
struct StateSourceTuple<StateContract<TDefinitions...>> {
    /// <summary>Tuple containing one source slot per contract definition.</summary>
    using Type = std::tuple<StateSourceSlot<TDefinitions>...>;
};

/// <summary>Registers an observer against publisher-wide and contract-specific publication interfaces.</summary>
template<typename TContract>
struct StatePublisherContractObserverRegistrar;

template<typename... TDefinitions>
struct StatePublisherContractObserverRegistrar<StateContract<TDefinitions...>> {
    /// <summary>Registers an observer for publisher events and every typed state in the contract.</summary>
    template<typename TObservable, typename TObserver>
    static Observable::ObserverHandlePtr Register(
        TObservable& observable,
        TObserver* observer
    ) {
        return observable.template RegisterObserverAs<
            IStatePublisherObserver,
            IStatePublishedObserver<TDefinitions>...
        >(observer);
    }
};

/// <summary>Publishes typed local state values with epoch/revision metadata for a fixed state contract.</summary>
/// <typeparam name="TContract">State contract defining values that may be published.</typeparam>
template<typename TContract>
class StatePublisher final {
    class PublisherObservable final : public Observable::ThreadSafeObservable {
    public:
        void SourceRegistered(StateTypeId typeId) {
            ExecuteNotification([&](NotificationContext& notification) {
                notification.WithObservers<IStatePublisherObserver>(
                    [&](IStatePublisherObserver* observer) {
                        observer->OnStateSourceRegistered(typeId);
                    }
                );
            });
        }

        void SourceUnregistered(StateTypeId typeId) {
            ExecuteNotification([&](NotificationContext& notification) {
                notification.WithObservers<IStatePublisherObserver>(
                    [&](IStatePublisherObserver* observer) {
                        observer->OnStateSourceUnregistered(typeId);
                    }
                );
            });
        }

        template<typename TDefinition>
        void Published(const StateUpdate<StateValueType<TDefinition>>& update) {
            ExecuteNotification([&](NotificationContext& notification) {
                notification.WithObservers<IStatePublisherObserver>(
                    [&](IStatePublisherObserver* observer) {
                        observer->OnStatePublished(
                            StateTypeIdOf<TDefinition>,
                            update.Header.Epoch,
                            update.Header.Revision
                        );
                    }
                );
                notification.WithObservers<IStatePublishedObserver<TDefinition>>(
                    [&](IStatePublishedObserver<TDefinition>* observer) {
                        observer->OnStatePublished(
                            StateTag<TDefinition>{}, update
                        );
                    }
                );
            });
        }
    };

    DeviceIdentifier _origin{};
    StateEpoch _epoch = 1;
    typename StateSourceTuple<TContract>::Type _sources{};
    std::shared_ptr<PublisherObservable> _observable =
        std::make_shared<PublisherObservable>();
    mutable std::recursive_mutex _mutex;

    template<typename TDefinition>
    StateSourceSlot<TDefinition>& SourceSlot() {
        static_assert(
            TContract::template Contains<TDefinition>,
            "State definition is not part of this StateContract"
        );
        return std::get<TContract::template IndexOf<TDefinition>()>(_sources);
    }

    template<typename TDefinition>
    const StateSourceSlot<TDefinition>& SourceSlot() const {
        static_assert(
            TContract::template Contains<TDefinition>,
            "State definition is not part of this StateContract"
        );
        return std::get<TContract::template IndexOf<TDefinition>()>(_sources);
    }

    template<typename TDefinition, typename TValue>
    StateUpdate<StateValueType<TDefinition>> MakeUpdateLocked(
        TValue&& value,
        bool advanceRevision
    ) {
        auto& source = SourceSlot<TDefinition>();
        if (advanceRevision || source.Revision == 0) {
            ++source.Revision;
            if (source.Revision == 0) ++source.Revision;
        }
        StateUpdate<StateValueType<TDefinition>> update;
        update.Header.Origin = _origin;
        update.Header.TypeId = StateTypeIdOf<TDefinition>;
        update.Header.Epoch = _epoch;
        update.Header.Revision = source.Revision;
        update.Value = std::forward<TValue>(value);
        return update;
    }

public:
    /// <summary>Creates a state publisher for the supplied origin and non-zero epoch.</summary>
    explicit StatePublisher(
        const DeviceIdentifier& origin = DeviceIdentifier{},
        StateEpoch epoch = 1
    ) : _origin(origin), _epoch(epoch == 0 ? 1 : epoch) {}

    /// <summary>Registers an observer for publisher-level source and publication lifecycle events.</summary>
    Observable::ObserverHandlePtr RegisterObserver(
        IStatePublisherObserver* observer
    ) {
        return _observable->template RegisterObserverAs<
            IStatePublisherObserver
        >(observer);
    }

    /// <summary>Registers one observer for publisher-level events and every typed state in this contract.</summary>
    template<typename TObserver>
    Observable::ObserverHandlePtr RegisterContractObserver(TObserver* observer) {
        return StatePublisherContractObserverRegistrar<TContract>::Register(
            *_observable, observer
        );
    }

    /// <summary>Registers an observer for publications of one typed state definition.</summary>
    template<typename TDefinition>
    Observable::ObserverHandlePtr RegisterPublishedObserver(
        IStatePublishedObserver<TDefinition>* observer
    ) {
        static_assert(
            TContract::template Contains<TDefinition>,
            "State definition is not part of this StateContract"
        );
        return _observable->template RegisterObserverAs<
            IStatePublishedObserver<TDefinition>
        >(observer);
    }

    /// <summary>Unregisters a previously registered publisher observer.</summary>
    void UnregisterObserver(Observable::IObserver* observer) {
        _observable->UnregisterObserver(observer);
    }

    /// <summary>Gets the device identifier written into published state metadata.</summary>
    const DeviceIdentifier& Origin() const noexcept { return _origin; }
    /// <summary>Gets the publisher epoch written into published state metadata.</summary>
    StateEpoch Epoch() const noexcept { return _epoch; }

    /// <summary>Registers a callable that supplies the current value for a typed state.</summary>
    /// <typeparam name="TDefinition">State definition whose source is registered.</typeparam>
    template<typename TDefinition>
    bool RegisterSource(std::function<StateValueType<TDefinition>()> source) {
        if (!source) return false;
        {
            std::lock_guard<std::recursive_mutex> lock(_mutex);
            SourceSlot<TDefinition>().Source = std::move(source);
        }
        _observable->SourceRegistered(StateTypeIdOf<TDefinition>);
        return true;
    }

    /// <summary>Removes the callable source registered for a typed state.</summary>
    template<typename TDefinition>
    bool UnregisterSource() {
        bool hadSource = false;
        {
            std::lock_guard<std::recursive_mutex> lock(_mutex);
            auto& source = SourceSlot<TDefinition>();
            hadSource = static_cast<bool>(source.Source);
            source.Source = {};
        }
        if (hadSource) {
            _observable->SourceUnregistered(StateTypeIdOf<TDefinition>);
        }
        return hadSource;
    }

    /// <summary>Indicates whether a callable source is registered for a typed state.</summary>
    template<typename TDefinition>
    bool HasSource() const {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        return static_cast<bool>(SourceSlot<TDefinition>().Source);
    }

    /// <summary>Reads the registered source, advances its revision, and publishes the resulting typed update.</summary>
    template<typename TDefinition>
    bool Publish() {
        StateUpdate<StateValueType<TDefinition>> update;
        {
            // Source callbacks are expected to be small local State getters.
            // Holding the recursive lock avoids copying std::function (which can
            // itself own heap storage) on every publication while remaining
            // re-entrant for StatePublisher calls from a source callback.
            std::lock_guard<std::recursive_mutex> lock(_mutex);
            auto& source = SourceSlot<TDefinition>();
            if (!source.Source) return false;
            auto value = source.Source();
            update = MakeUpdateLocked<TDefinition>(std::move(value), true);
        }
        _observable->template Published<TDefinition>(update);
        return true;
    }

    /// <summary>Copies a supplied typed value into a new publication and advances its revision.</summary>
    template<typename TDefinition>
    bool Publish(const StateValueType<TDefinition>& value) {
        StateUpdate<StateValueType<TDefinition>> update;
        {
            std::lock_guard<std::recursive_mutex> lock(_mutex);
            update = MakeUpdateLocked<TDefinition>(value, true);
        }
        _observable->template Published<TDefinition>(update);
        return true;
    }

    /// <summary>Moves a supplied typed value into a new publication and advances its revision.</summary>
    template<typename TDefinition>
    bool Publish(StateValueType<TDefinition>&& value) {
        StateUpdate<StateValueType<TDefinition>> update;
        {
            std::lock_guard<std::recursive_mutex> lock(_mutex);
            update = MakeUpdateLocked<TDefinition>(std::move(value), true);
        }
        _observable->template Published<TDefinition>(update);
        return true;
    }

    /// <summary>Reads a registered source into a transport update without advancing an existing revision.</summary>
    template<typename TDefinition>
    bool Snapshot(StateUpdate<StateValueType<TDefinition>>& update) {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        auto& source = SourceSlot<TDefinition>();
        if (!source.Source) return false;
        auto value = source.Source();
        update = MakeUpdateLocked<TDefinition>(std::move(value), false);
        return true;
    }
};

} // namespace State
} // namespace ESPressio
