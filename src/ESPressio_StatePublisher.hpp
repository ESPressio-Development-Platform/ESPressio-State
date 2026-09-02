#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <tuple>
#include <utility>

#include <ESPressio_Memory.hpp>
#include <ESPressio_Synchronization.hpp>
#include <ESPressio_ThreadSafeObservable.hpp>

#include "ESPressio_DeviceIdentifier.hpp"
#include "ESPressio_StateComparison.hpp"
#include "ESPressio_StateContract.hpp"
#include "ESPressio_StateObservers.hpp"
#include "ESPressio_StateTransport.hpp"

namespace ESPressio {
namespace State {

template<typename TDefinition>
struct StateSourceSlot {
    using Value = StateValueType<TDefinition>;
    using SourceCallback = std::function<Value()>;
    std::shared_ptr<const SourceCallback> Source;
    std::optional<Value> LastPublished;
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
    static Observable::ObserverHandlePtr Register(TObservable& observable, TObserver* observer) {
        return observable.template RegisterObserverAs<
            IStatePublisherObserver,
            IStatePublishedObserver<TDefinitions>...
        >(observer);
    }
};

/// <summary>Publishes typed local state values with epoch/revision metadata for a fixed state contract.</summary>
/// <typeparam name="TContract">State contract defining values that may be published.</typeparam>
/// <remarks>Only semantic changes advance revisions or notify observers. Publisher mutation is serialized by a System-provided mutex. Registered source callables are pinned in external-preferred immutable shared storage, invoked without the publisher mutex held, and revalidated before their result is committed.</remarks>
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
                        observer->OnStatePublished(StateTag<TDefinition>{}, update);
                    }
                );
            });
        }
    };

    DeviceIdentifier _origin{};
    StateEpoch _epoch = 1;
    typename StateSourceTuple<TContract>::Type _sources{};
    std::shared_ptr<PublisherObservable> _observable;
    mutable System::Synchronization::Mutex _mutex;

    std::shared_ptr<PublisherObservable> EnsureObservable() {
        std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
        if (_observable) return _observable;
        try {
            _observable = System::Memory::MakeShared<
                PublisherObservable,
                System::Memory::MemoryPolicy::ExternalPreferred
            >();
        } catch (...) {
            return {};
        }
        return _observable;
    }

    std::shared_ptr<PublisherObservable> ObservableSnapshot() const {
        std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
        return _observable;
    }

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

    template<typename TDefinition>
    bool IsMeaningfulChangeLocked(const StateValueType<TDefinition>& value) const {
        const auto& source = SourceSlot<TDefinition>();
        return !source.LastPublished.has_value() ||
            StateValueChanged<TDefinition>(*source.LastPublished, value);
    }

    template<typename TDefinition>
    void RememberPublishedLocked(const StateValueType<TDefinition>& value) {
        SourceSlot<TDefinition>().LastPublished = value;
    }

    template<typename TDefinition>
    using SourceRegistration = std::shared_ptr<
        const typename StateSourceSlot<TDefinition>::SourceCallback
    >;

public:
    explicit StatePublisher(
        const DeviceIdentifier& origin = DeviceIdentifier{},
        StateEpoch epoch = 1
    ) : _origin(origin), _epoch(epoch == 0 ? 1 : epoch) {}

    Observable::ObserverHandlePtr RegisterObserver(IStatePublisherObserver* observer) {
        auto observable = EnsureObservable();
        return observable
            ? observable->template RegisterObserverAs<IStatePublisherObserver>(observer)
            : Observable::ObserverHandlePtr{};
    }

    template<typename TObserver>
    Observable::ObserverHandlePtr RegisterContractObserver(TObserver* observer) {
        auto observable = EnsureObservable();
        return observable
            ? StatePublisherContractObserverRegistrar<TContract>::Register(
                *observable, observer
            )
            : Observable::ObserverHandlePtr{};
    }

    template<typename TDefinition>
    Observable::ObserverHandlePtr RegisterPublishedObserver(
        IStatePublishedObserver<TDefinition>* observer
    ) {
        static_assert(
            TContract::template Contains<TDefinition>,
            "State definition is not part of this StateContract"
        );
        auto observable = EnsureObservable();
        return observable
            ? observable->template RegisterObserverAs<
                IStatePublishedObserver<TDefinition>
            >(observer)
            : Observable::ObserverHandlePtr{};
    }

    void UnregisterObserver(Observable::IObserver* observer) {
        auto observable = ObservableSnapshot();
        if (observable) observable->UnregisterObserver(observer);
    }

    const DeviceIdentifier& Origin() const noexcept { return _origin; }
    StateEpoch Epoch() const noexcept { return _epoch; }

    template<typename TDefinition>
    bool RegisterSource(std::function<StateValueType<TDefinition>()> source) {
        if (!source) return false;
        auto observable = EnsureObservable();
        if (!observable) return false;

        SourceRegistration<TDefinition> registration;
        try {
            registration = System::Memory::MakeShared<
                typename StateSourceSlot<TDefinition>::SourceCallback,
                System::Memory::MemoryPolicy::ExternalPreferred
            >(std::move(source));
        } catch (...) {
            return false;
        }

        {
            std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
            auto& slot = SourceSlot<TDefinition>();
            slot.Source = std::move(registration);
            slot.LastPublished.reset();
        }
        observable->SourceRegistered(StateTypeIdOf<TDefinition>);
        return true;
    }

    template<typename TDefinition>
    bool UnregisterSource() {
        bool hadSource = false;
        {
            std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
            auto& source = SourceSlot<TDefinition>();
            hadSource = static_cast<bool>(source.Source);
            source.Source.reset();
            source.LastPublished.reset();
        }
        auto observable = ObservableSnapshot();
        if (hadSource && observable) {
            observable->SourceUnregistered(StateTypeIdOf<TDefinition>);
        }
        return hadSource;
    }

    template<typename TDefinition>
    bool HasSource() const {
        std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
        return static_cast<bool>(SourceSlot<TDefinition>().Source);
    }

    template<typename TDefinition>
    bool Publish() {
        auto observable = EnsureObservable();
        if (!observable) return false;

        SourceRegistration<TDefinition> source;
        {
            std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
            source = SourceSlot<TDefinition>().Source;
        }
        if (!source) return false;

        // Source code is consumer-provided and may synchronously re-enter this
        // publisher or block on other subsystems. Never invoke it under _mutex.
        auto value = (*source)();

        StateUpdate<StateValueType<TDefinition>> update;
        bool changed = false;
        {
            std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
            auto& current = SourceSlot<TDefinition>();
            if (current.Source != source) {
                return false;
            }
            changed = IsMeaningfulChangeLocked<TDefinition>(value);
            if (changed) {
                RememberPublishedLocked<TDefinition>(value);
                update = MakeUpdateLocked<TDefinition>(std::move(value), true);
            }
        }
        if (changed) observable->template Published<TDefinition>(update);
        return true;
    }

    template<typename TDefinition>
    bool Publish(const StateValueType<TDefinition>& value) {
        auto observable = EnsureObservable();
        if (!observable) return false;
        StateUpdate<StateValueType<TDefinition>> update;
        bool changed = false;
        {
            std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
            changed = IsMeaningfulChangeLocked<TDefinition>(value);
            if (changed) {
                RememberPublishedLocked<TDefinition>(value);
                update = MakeUpdateLocked<TDefinition>(value, true);
            }
        }
        if (changed) observable->template Published<TDefinition>(update);
        return true;
    }

    template<typename TDefinition>
    bool Publish(StateValueType<TDefinition>&& value) {
        auto observable = EnsureObservable();
        if (!observable) return false;
        StateUpdate<StateValueType<TDefinition>> update;
        bool changed = false;
        {
            std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
            changed = IsMeaningfulChangeLocked<TDefinition>(value);
            if (changed) {
                RememberPublishedLocked<TDefinition>(value);
                update = MakeUpdateLocked<TDefinition>(std::move(value), true);
            }
        }
        if (changed) observable->template Published<TDefinition>(update);
        return true;
    }

    template<typename TDefinition>
    bool Snapshot(StateUpdate<StateValueType<TDefinition>>& update) {
        SourceRegistration<TDefinition> source;
        {
            std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
            source = SourceSlot<TDefinition>().Source;
        }
        if (!source) return false;

        auto value = (*source)();

        {
            std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
            auto& current = SourceSlot<TDefinition>();
            if (current.Source != source) {
                return false;
            }
            update = MakeUpdateLocked<TDefinition>(std::move(value), false);
        }
        return true;
    }
};

} // namespace State
} // namespace ESPressio
