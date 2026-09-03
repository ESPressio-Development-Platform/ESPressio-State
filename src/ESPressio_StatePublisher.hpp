#pragma once

#include <cstddef>
#include <memory>
#include <mutex>
#include <tuple>
#include <utility>

#include <ESPressio_Memory.hpp>
#include <ESPressio_Synchronization.hpp>
#include <ESPressio_ThreadSafeObservable.hpp>

#include "ESPressio_LocalStateRegistry.hpp"
#include "ESPressio_StateAddress.hpp"
#include "ESPressio_StateAvailability.hpp"
#include "ESPressio_StateContract.hpp"
#include "ESPressio_StateObservers.hpp"
#include "ESPressio_StateTransport.hpp"

namespace ESPressio {
namespace State {

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

/// <summary>
/// Publishes application-owned authoritative State through one local registration/lineage authority.
/// </summary>
/// <typeparam name="TContract">Closed set of State definitions that may be bound and published.</typeparam>
/// <typeparam name="TMaximumObservers">Maximum simultaneous publisher observer registrations.</typeparam>
/// <remarks>
/// StatePublisher owns no duplicate canonical State values and no independent revision table. It delegates
/// binding, epoch and revision authority to LocalStateRegistry. Application code mutates its own bound value
/// and then calls NotifyChanged; that explicit call advances the registration revision and synchronously
/// emits the immutable typed publication snapshot observed by transport/adapters. Binding/unbinding emits
/// lifecycle and authoritative availability changes separately from value publication.
///
/// Publication notification is serialized per State definition. A same-State NotifyChanged call made from
/// inside a publication callback is accepted into a finite deferred queue and emitted only after the current
/// notification has completed. Accepted revisions therefore remain non-reentrant and are observed in strict
/// revision order. Queue exhaustion is explicit backpressure: the nested NotifyChanged call returns false
/// before a new revision is committed.
/// </remarks>
template<typename TContract, std::size_t TMaximumObservers = 8>
class StatePublisher final {
    static_assert(TMaximumObservers > 0, "StatePublisher observer capacity must be non-zero");

    /// <summary>Implementation bound for accepted same-State changes raised from inside notification.</summary>
    static constexpr std::size_t DeferredNotificationCapacity = 4;
    static constexpr auto ExternalPreferred = System::Memory::MemoryPolicy::ExternalPreferred;

    class PublisherObservable final : public Observable::ThreadSafeObservable {
    public:
        void SourceBound(const StateAddress& address, StateEpoch epoch, StateRevision revision) {
            ExecuteNotification([&](NotificationContext& notification) {
                notification.WithObservers<IStatePublisherObserver>(
                    [&](IStatePublisherObserver* observer) {
                        observer->OnStateSourceBound(address, epoch, revision);
                    }
                );
            });
        }

        void SourceUnbound(
            const StateAddress& address,
            StateUnbindMode mode,
            StateEpoch epoch,
            StateRevision revision
        ) {
            ExecuteNotification([&](NotificationContext& notification) {
                notification.WithObservers<IStatePublisherObserver>(
                    [&](IStatePublisherObserver* observer) {
                        observer->OnStateSourceUnbound(address, mode, epoch, revision);
                    }
                );
            });
        }

        void AvailabilityChanged(
            const StateAddress& address,
            StateAvailabilityStatus previous,
            StateAvailabilityStatus current
        ) {
            ExecuteNotification([&](NotificationContext& notification) {
                notification.WithObservers<IStatePublisherObserver>(
                    [&](IStatePublisherObserver* observer) {
                        observer->OnStateAvailabilityChanged(address, previous, current);
                    }
                );
            });
        }

        template<typename TDefinition>
        void Published(const StateUpdate<StateValueType<TDefinition>>& update) {
            const StateAddress address{
                update.Header.Origin,
                update.Header.TypeId
            };
            ExecuteNotification([&](NotificationContext& notification) {
                notification.WithObservers<IStatePublisherObserver>(
                    [&](IStatePublisherObserver* observer) {
                        observer->OnStatePublished(
                            address,
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

    template<typename TDefinition>
    struct NotificationState final {
        using Update = StateUpdate<StateValueType<TDefinition>>;
        using Queue = System::Memory::Vector<Update, ExternalPreferred>;

        bool Dispatching = false;
        Queue Deferred{};
    };

    template<typename T>
    struct NotificationStateTuple;

    template<typename... TDefinitions>
    struct NotificationStateTuple<StateContract<TDefinitions...>> {
        using Type = std::tuple<NotificationState<TDefinitions>...>;
    };

    DeviceIdentifier _origin{};
    LocalStateRegistry<TContract> _registry{};
    typename NotificationStateTuple<TContract>::Type _notificationStates{};
    std::shared_ptr<PublisherObservable> _observable;
    mutable System::Synchronization::RecursiveMutex _observableMutex;
    mutable System::Synchronization::RecursiveMutex _publicationMutex;

    std::shared_ptr<PublisherObservable> EnsureObservable() {
        std::lock_guard<System::Synchronization::RecursiveMutex> lock(_observableMutex);
        if (_observable) return _observable;
        try {
            _observable = System::Memory::MakeShared<
                PublisherObservable,
                ExternalPreferred
            >();
        } catch (...) {
            return {};
        }
        return _observable;
    }

    std::shared_ptr<PublisherObservable> ObservableSnapshot() const {
        std::lock_guard<System::Synchronization::RecursiveMutex> lock(_observableMutex);
        return _observable;
    }

    template<typename TRegistrar>
    Observable::ObserverHandlePtr RegisterBounded(TRegistrar&& registrar) {
        auto observable = EnsureObservable();
        if (!observable) return {};
        std::lock_guard<System::Synchronization::RecursiveMutex> lock(_observableMutex);
        if (observable->GetObserverCount() >= TMaximumObservers) return {};
        return registrar(*observable);
    }

    template<typename TDefinition>
    NotificationState<TDefinition>& GetNotificationState() {
        static_assert(TContract::template Contains<TDefinition>, "State definition is not part of this StateContract");
        return std::get<TContract::template IndexOf<TDefinition>()>(_notificationStates);
    }

    template<typename TDefinition>
    bool EnsureDeferredCapacityLocked(NotificationState<TDefinition>& state) {
        if (state.Deferred.capacity() >= DeferredNotificationCapacity) return true;
        try {
            state.Deferred.reserve(DeferredNotificationCapacity);
            return true;
        } catch (...) {
            return false;
        }
    }

    template<typename TDefinition>
    void DispatchSerialized(StateUpdate<StateValueType<TDefinition>> update) {
        auto observable = ObservableSnapshot();
        if (observable) observable->template Published<TDefinition>(update);

        while (true) {
            bool haveNext = false;
            {
                std::lock_guard<System::Synchronization::RecursiveMutex> lock(_publicationMutex);
                auto& state = GetNotificationState<TDefinition>();
                if (state.Deferred.empty()) {
                    state.Dispatching = false;
                    return;
                }
                update = std::move(state.Deferred.front());
                state.Deferred.erase(state.Deferred.begin());
                haveNext = true;
            }
            if (haveNext) {
                observable = ObservableSnapshot();
                if (observable) observable->template Published<TDefinition>(update);
            }
        }
    }

    static StateAvailabilityStatus AvailableStatus() noexcept {
        return {StateAvailability::Available, StateAvailabilityReason::None};
    }

    static StateAvailabilityStatus UnboundStatus() noexcept {
        return {StateAvailability::Unavailable, StateAvailabilityReason::SourceUnbound};
    }

public:
    /// <summary>Maximum number of simultaneous observer registrations accepted by this publisher.</summary>
    static constexpr std::size_t MaximumObservers = TMaximumObservers;

    /// <summary>Maximum accepted same-State notifications deferred behind one active notification.</summary>
    static constexpr std::size_t MaximumDeferredNotificationsPerState = DeferredNotificationCapacity;

    /// <summary>Creates a publisher for one permanent authoritative device identity.</summary>
    explicit StatePublisher(const DeviceIdentifier& origin = DeviceIdentifier{})
        : _origin(origin) {}

    /// <summary>Returns the permanent device identity carried by this publisher's State addresses.</summary>
    const DeviceIdentifier& Origin() const noexcept { return _origin; }

    /// <summary>Registers one lifecycle observer when bounded observer capacity remains.</summary>
    Observable::ObserverHandlePtr RegisterObserver(IStatePublisherObserver* observer) {
        if (observer == nullptr) return {};
        return RegisterBounded([&](PublisherObservable& observable) {
            return observable.template RegisterObserverAs<IStatePublisherObserver>(observer);
        });
    }

    /// <summary>Registers one observer against publisher lifecycle plus every typed State in the contract.</summary>
    template<typename TObserver>
    Observable::ObserverHandlePtr RegisterContractObserver(TObserver* observer) {
        if (observer == nullptr) return {};
        return RegisterBounded([&](PublisherObservable& observable) {
            return StatePublisherContractObserverRegistrar<TContract>::Register(
                observable,
                observer
            );
        });
    }

    /// <summary>Registers one typed publication observer when bounded observer capacity remains.</summary>
    template<typename TDefinition>
    Observable::ObserverHandlePtr RegisterPublishedObserver(
        IStatePublishedObserver<TDefinition>* observer
    ) {
        static_assert(TContract::template Contains<TDefinition>, "State definition is not part of this StateContract");
        if (observer == nullptr) return {};
        return RegisterBounded([&](PublisherObservable& observable) {
            return observable.template RegisterObserverAs<IStatePublishedObserver<TDefinition>>(observer);
        });
    }

    /// <summary>Explicitly unregisters an observer; destroying its RAII handle has the same effect.</summary>
    void UnregisterObserver(Observable::IObserver* observer) {
        auto observable = ObservableSnapshot();
        if (observable) observable->UnregisterObserver(observer);
    }

    /// <summary>Binds one application-owned value as the sole local authority for the State definition.</summary>
    template<typename TDefinition>
    bool Bind(StateValueType<TDefinition>& source) {
        if (!_origin) return false;
        if (!_registry.template Bind<TDefinition>(source)) return false;

        const auto registration = _registry.template Registration<TDefinition>();
        auto observable = ObservableSnapshot();
        if (observable) {
            const auto address = MakeStateAddress<TDefinition>(_origin);
            observable->SourceBound(address, registration.Epoch, registration.Revision);
            observable->AvailabilityChanged(address, UnboundStatus(), AvailableStatus());
        }
        return true;
    }

    /// <summary>
    /// Removes one bound source while preserving or discarding its registration lineage according to mode.
    /// </summary>
    template<typename TDefinition>
    bool Unbind(StateUnbindMode mode) {
        const auto before = _registry.template Registration<TDefinition>();
        if (!before.Bound) return false;
        if (!_registry.template Unbind<TDefinition>(mode)) return false;

        auto observable = ObservableSnapshot();
        if (observable) {
            const auto address = MakeStateAddress<TDefinition>(_origin);
            observable->AvailabilityChanged(address, AvailableStatus(), UnboundStatus());
            observable->SourceUnbound(
                address,
                mode,
                before.Epoch,
                before.Revision
            );
        }
        return true;
    }

    /// <summary>
    /// Advances the local registration revision and synchronously publishes the current authoritative value.
    /// </summary>
    /// <remarks>
    /// This is the authoritative mutation boundary. Equality is deliberately not re-evaluated here: the caller has
    /// explicitly declared that the State changed. The value is copied once into the immutable StateUpdate snapshot
    /// emitted to observers; the application-owned source remains the canonical local value. If the same State is
    /// already notifying, the immutable snapshot is queued behind it. If that finite queue is full, this call returns
    /// false before advancing the registration revision, providing deterministic backpressure without losing an
    /// already-committed revision.
    /// </remarks>
    template<typename TDefinition>
    bool NotifyChanged(StateRevision* committedRevision = nullptr) {
        StateUpdate<StateValueType<TDefinition>> update;
        bool dispatchNow = false;
        StateRevision revision = 0;

        {
            std::lock_guard<System::Synchronization::RecursiveMutex> publicationLock(_publicationMutex);
            auto& notificationState = GetNotificationState<TDefinition>();

            if (notificationState.Dispatching) {
                if (!EnsureDeferredCapacityLocked(notificationState)) return false;
                if (notificationState.Deferred.size() >= DeferredNotificationCapacity) return false;
            }

            if (!_registry.template NotifyChanged<TDefinition>(revision)) return false;

            LocalStateView<TDefinition> view;
            if (!_registry.template Read<TDefinition>(view)) return false;
            if (view.Revision != revision) return false;

            update.Header.Origin = _origin;
            update.Header.TypeId = StateTypeIdOf<TDefinition>;
            update.Header.Epoch = view.Epoch;
            update.Header.Revision = view.Revision;
            update.Value = view.ValueRef();

            if (notificationState.Dispatching) {
                try {
                    notificationState.Deferred.push_back(std::move(update));
                } catch (...) {
                    return false;
                }
            } else {
                notificationState.Dispatching = true;
                dispatchNow = true;
            }
        }

        if (committedRevision != nullptr) *committedRevision = revision;
        if (dispatchNow) DispatchSerialized<TDefinition>(std::move(update));
        return true;
    }

    /// <summary>Reads the currently bound application-owned value without changing revision or publication state.</summary>
    template<typename TDefinition>
    bool Read(LocalStateView<TDefinition>& view) const noexcept {
        return _registry.template Read<TDefinition>(view);
    }

    /// <summary>Returns local registration metadata even when Retain has left the State temporarily unbound.</summary>
    template<typename TDefinition>
    LocalStateRegistrationSnapshot Registration() const noexcept {
        return _registry.template Registration<TDefinition>();
    }

    /// <summary>
    /// Captures the current authoritative value for subscription establishment or resynchronization without advancing revision.
    /// </summary>
    template<typename TDefinition>
    bool Snapshot(StateUpdate<StateValueType<TDefinition>>& update) const {
        LocalStateView<TDefinition> view;
        if (!_registry.template Read<TDefinition>(view)) return false;
        if (view.Revision == 0 || !_origin) return false;

        update.Header.Origin = _origin;
        update.Header.TypeId = StateTypeIdOf<TDefinition>;
        update.Header.Epoch = view.Epoch;
        update.Header.Revision = view.Revision;
        update.Value = view.ValueRef();
        return true;
    }
};

} // namespace State
} // namespace ESPressio
