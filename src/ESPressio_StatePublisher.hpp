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

template<typename TDefinition>
struct StateSourceSlot {
    using Value = StateValueType<TDefinition>;
    std::function<Value()> Source;
    StateRevision Revision = 0;
};

template<typename TContract>
struct StateSourceTuple;

template<typename... TDefinitions>
struct StateSourceTuple<StateContract<TDefinitions...>> {
    using Type = std::tuple<StateSourceSlot<TDefinitions>...>;
};

template<typename TContract>
struct StatePublisherContractObserverRegistrar;

template<typename... TDefinitions>
struct StatePublisherContractObserverRegistrar<StateContract<TDefinitions...>> {
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
    explicit StatePublisher(
        const DeviceIdentifier& origin = DeviceIdentifier{},
        StateEpoch epoch = 1
    ) : _origin(origin), _epoch(epoch == 0 ? 1 : epoch) {}

    Observable::ObserverHandlePtr RegisterObserver(
        IStatePublisherObserver* observer
    ) {
        return _observable->template RegisterObserverAs<
            IStatePublisherObserver
        >(observer);
    }

    template<typename TObserver>
    Observable::ObserverHandlePtr RegisterContractObserver(TObserver* observer) {
        return StatePublisherContractObserverRegistrar<TContract>::Register(
            *_observable, observer
        );
    }

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

    void UnregisterObserver(Observable::IObserver* observer) {
        _observable->UnregisterObserver(observer);
    }

    const DeviceIdentifier& Origin() const noexcept { return _origin; }
    StateEpoch Epoch() const noexcept { return _epoch; }

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

    template<typename TDefinition>
    bool HasSource() const {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        return static_cast<bool>(SourceSlot<TDefinition>().Source);
    }

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
