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
/// emits the immutable typed publication snapshot observed by transport/adapters.
///
/// Every notification belonging to one State definition is serialized through a State-owned operation lane.
/// A same-State Bind, Unbind or NotifyChanged raised from inside an active callback is accepted only when the
/// finite deferred-operation lane has capacity, and its notifications run after the current operation returns.
/// Consecutive deferred Publication operations may replace one another with the newest immutable snapshot,
/// because State is latest-fact data. Lifecycle/availability operations are never silently coalesced. User
/// callbacks are always invoked after publisher/registry mutation locks have been released.
/// </remarks>
template<typename TContract, std::size_t TMaximumObservers = 8>
class StatePublisher final {
    static_assert(TMaximumObservers > 0, "StatePublisher observer capacity must be non-zero");

    static constexpr auto ExternalPreferred = System::Memory::MemoryPolicy::ExternalPreferred;
    static constexpr std::size_t MaximumDeferredOperationsPerState = 4;

    class PublisherObservable final : public Observable::ThreadSafeObservable {
    public:
        void SourceBound(const StateAddress& address, StateEpoch epoch, StateRevision revision) {
            ExecuteNotification([&](NotificationContext& notification) {
                notification.WithObservers<IStatePublisherObserver>([&](IStatePublisherObserver* observer) {
                    observer->OnStateSourceBound(address, epoch, revision);
                });
            });
        }

        void SourceUnbound(const StateAddress& address, StateUnbindMode mode, StateEpoch epoch, StateRevision revision) {
            ExecuteNotification([&](NotificationContext& notification) {
                notification.WithObservers<IStatePublisherObserver>([&](IStatePublisherObserver* observer) {
                    observer->OnStateSourceUnbound(address, mode, epoch, revision);
                });
            });
        }

        void AvailabilityChanged(const StateAddress& address, StateAvailabilityStatus previous, StateAvailabilityStatus current) {
            ExecuteNotification([&](NotificationContext& notification) {
                notification.WithObservers<IStatePublisherObserver>([&](IStatePublisherObserver* observer) {
                    observer->OnStateAvailabilityChanged(address, previous, current);
                });
            });
        }

        template<typename TDefinition>
        void Published(const StateUpdate<StateValueType<TDefinition>>& update) {
            const StateAddress address{update.Header.Origin, update.Header.TypeId};
            ExecuteNotification([&](NotificationContext& notification) {
                notification.WithObservers<IStatePublisherObserver>([&](IStatePublisherObserver* observer) {
                    observer->OnStatePublished(address, update.Header.Epoch, update.Header.Revision);
                });
                notification.WithObservers<IStatePublishedObserver<TDefinition>>([&](IStatePublishedObserver<TDefinition>* observer) {
                    observer->OnStatePublished(StateTag<TDefinition>{}, update);
                });
            });
        }
    };

    enum class OperationKind : uint8_t {
        Bound = 0,
        Unbound,
        Publication
    };

    template<typename TDefinition>
    struct DeferredOperation final {
        using Update = StateUpdate<StateValueType<TDefinition>>;

        OperationKind Kind = OperationKind::Bound;
        StateEpoch Epoch = 0;
        StateRevision Revision = 0;
        StateUnbindMode UnbindMode = StateUnbindMode::Retain;
        std::shared_ptr<Update> Publication;
    };

    template<typename TDefinition>
    struct NotificationState final {
        using Operation = DeferredOperation<TDefinition>;
        using Queue = System::Memory::Vector<Operation, ExternalPreferred>;

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
            _observable = System::Memory::MakeShared<PublisherObservable, ExternalPreferred>();
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
    bool PrepareDeferredCapacityLocked(NotificationState<TDefinition>& state) {
        if (!state.Dispatching) return true;
        if (!state.Deferred.empty() && state.Deferred.back().Kind == OperationKind::Publication) return true;
        if (state.Deferred.size() >= MaximumDeferredOperationsPerState) return false;
        if (state.Deferred.capacity() >= MaximumDeferredOperationsPerState) return true;
        try {
            state.Deferred.reserve(MaximumDeferredOperationsPerState);
            return true;
        } catch (...) {
            return false;
        }
    }

    template<typename TDefinition>
    bool EnqueueOperationLocked(NotificationState<TDefinition>& state, DeferredOperation<TDefinition>&& operation) {
        if (!state.Dispatching) return false;

        if (
            operation.Kind == OperationKind::Publication &&
            !state.Deferred.empty() &&
            state.Deferred.back().Kind == OperationKind::Publication
        ) {
            state.Deferred.back() = std::move(operation);
            return true;
        }

        if (state.Deferred.size() >= MaximumDeferredOperationsPerState) return false;
        try {
            state.Deferred.push_back(std::move(operation));
            return true;
        } catch (...) {
            return false;
        }
    }

    template<typename TDefinition>
    void DispatchOperation(const DeferredOperation<TDefinition>& operation) {
        auto observable = ObservableSnapshot();
        if (!observable) return;

        const auto address = MakeStateAddress<TDefinition>(_origin);
        switch (operation.Kind) {
            case OperationKind::Bound:
                observable->SourceBound(address, operation.Epoch, operation.Revision);
                observable->AvailabilityChanged(address, UnboundStatus(), AvailableStatus());
                break;
            case OperationKind::Unbound:
                observable->AvailabilityChanged(address, AvailableStatus(), UnboundStatus());
                observable->SourceUnbound(address, operation.UnbindMode, operation.Epoch, operation.Revision);
                break;
            case OperationKind::Publication:
                if (operation.Publication) observable->template Published<TDefinition>(*operation.Publication);
                break;
        }
    }

    template<typename TDefinition>
    void DrainSerialized(DeferredOperation<TDefinition> operation) {
        try {
            while (true) {
                DispatchOperation<TDefinition>(operation);
                {
                    std::lock_guard<System::Synchronization::RecursiveMutex> lock(_publicationMutex);
                    auto& state = GetNotificationState<TDefinition>();
                    if (state.Deferred.empty()) {
                        state.Dispatching = false;
                        return;
                    }
                    operation = std::move(state.Deferred.front());
                    state.Deferred.erase(state.Deferred.begin());
                }
            }
        } catch (...) {
            std::lock_guard<System::Synchronization::RecursiveMutex> lock(_publicationMutex);
            auto& state = GetNotificationState<TDefinition>();
            state.Deferred.clear();
            state.Dispatching = false;
            throw;
        }
    }

    static StateAvailabilityStatus AvailableStatus() noexcept {
        return {StateAvailability::Available, StateAvailabilityReason::None};
    }

    static StateAvailabilityStatus UnboundStatus() noexcept {
        return {StateAvailability::Unavailable, StateAvailabilityReason::SourceUnbound};
    }

public:
    /// <summary>Maximum simultaneous publisher observer registrations.</summary>
    static constexpr std::size_t MaximumObservers = TMaximumObservers;

    explicit StatePublisher(const DeviceIdentifier& origin = DeviceIdentifier{}) : _origin(origin) {}

    const DeviceIdentifier& Origin() const noexcept { return _origin; }

    Observable::ObserverHandlePtr RegisterObserver(IStatePublisherObserver* observer) {
        if (observer == nullptr) return {};
        return RegisterBounded([&](PublisherObservable& observable) {
            return observable.template RegisterObserverAs<IStatePublisherObserver>(observer);
        });
    }

    template<typename TObserver>
    Observable::ObserverHandlePtr RegisterContractObserver(TObserver* observer) {
        if (observer == nullptr) return {};
        return RegisterBounded([&](PublisherObservable& observable) {
            return StatePublisherContractObserverRegistrar<TContract>::Register(observable, observer);
        });
    }

    template<typename TDefinition>
    Observable::ObserverHandlePtr RegisterPublishedObserver(IStatePublishedObserver<TDefinition>* observer) {
        static_assert(TContract::template Contains<TDefinition>, "State definition is not part of this StateContract");
        if (observer == nullptr) return {};
        return RegisterBounded([&](PublisherObservable& observable) {
            return observable.template RegisterObserverAs<IStatePublishedObserver<TDefinition>>(observer);
        });
    }

    void UnregisterObserver(Observable::IObserver* observer) {
        auto observable = ObservableSnapshot();
        if (observable) observable->UnregisterObserver(observer);
    }

    /// <summary>Binds one application-owned value as the sole local authority for the State definition.</summary>
    template<typename TDefinition>
    bool Bind(StateValueType<TDefinition>& source) {
        DeferredOperation<TDefinition> operation;
        bool dispatchNow = false;
        {
            std::lock_guard<System::Synchronization::RecursiveMutex> publicationLock(_publicationMutex);
            auto& state = GetNotificationState<TDefinition>();
            if (!PrepareDeferredCapacityLocked(state)) return false;
            if (!_origin || !_registry.template Bind<TDefinition>(source)) return false;

            const auto registration = _registry.template Registration<TDefinition>();
            operation.Kind = OperationKind::Bound;
            operation.Epoch = registration.Epoch;
            operation.Revision = registration.Revision;

            if (state.Dispatching) {
                if (!EnqueueOperationLocked(state, std::move(operation))) return false;
            } else {
                state.Dispatching = true;
                dispatchNow = true;
            }
        }

        if (dispatchNow) DrainSerialized<TDefinition>(std::move(operation));
        return true;
    }

    /// <summary>Removes one bound source while preserving or discarding its registration lineage according to mode.</summary>
    template<typename TDefinition>
    bool Unbind(StateUnbindMode mode) {
        DeferredOperation<TDefinition> operation;
        bool dispatchNow = false;
        {
            std::lock_guard<System::Synchronization::RecursiveMutex> publicationLock(_publicationMutex);
            auto& state = GetNotificationState<TDefinition>();
            if (!PrepareDeferredCapacityLocked(state)) return false;

            const auto before = _registry.template Registration<TDefinition>();
            if (!before.Bound || !_registry.template Unbind<TDefinition>(mode)) return false;

            operation.Kind = OperationKind::Unbound;
            operation.Epoch = before.Epoch;
            operation.Revision = before.Revision;
            operation.UnbindMode = mode;

            if (state.Dispatching) {
                if (!EnqueueOperationLocked(state, std::move(operation))) return false;
            } else {
                state.Dispatching = true;
                dispatchNow = true;
            }
        }

        if (dispatchNow) DrainSerialized<TDefinition>(std::move(operation));
        return true;
    }

    /// <summary>Advances the local registration revision and publishes the current authoritative value.</summary>
    /// <remarks>
    /// Equality is deliberately not re-evaluated here. The immutable publication payload and any deferred queue
    /// capacity are prepared before revision commit, so allocation/copy failure cannot consume a revision.
    /// Consecutive deferred publications are latest-fact coalesced, while intervening lifecycle operations
    /// preserve their exact order.
    /// </remarks>
    template<typename TDefinition>
    bool NotifyChanged(StateRevision* committedRevision = nullptr) {
        using Update = StateUpdate<StateValueType<TDefinition>>;

        DeferredOperation<TDefinition> operation;
        Update prepared;
        bool dispatchNow = false;
        StateRevision revision = 0;

        {
            std::lock_guard<System::Synchronization::RecursiveMutex> publicationLock(_publicationMutex);
            auto& state = GetNotificationState<TDefinition>();
            if (!PrepareDeferredCapacityLocked(state)) return false;

            LocalStateView<TDefinition> before;
            if (!_registry.template Read<TDefinition>(before)) return false;

            prepared.Header.Origin = _origin;
            prepared.Header.TypeId = StateTypeIdOf<TDefinition>;
            prepared.Header.Epoch = before.Epoch;
            try {
                prepared.Value = before.ValueRef();
                operation.Publication = System::Memory::MakeShared<Update, ExternalPreferred>(prepared);
            } catch (...) {
                return false;
            }

            if (!_registry.template NotifyChanged<TDefinition>(revision)) return false;
            prepared.Header.Revision = revision;
            operation.Publication->Header.Revision = revision;
            operation.Kind = OperationKind::Publication;
            operation.Epoch = before.Epoch;
            operation.Revision = revision;

            if (state.Dispatching) {
                if (!EnqueueOperationLocked(state, std::move(operation))) return false;
            } else {
                state.Dispatching = true;
                dispatchNow = true;
            }
        }

        if (committedRevision != nullptr) *committedRevision = revision;
        if (dispatchNow) DrainSerialized<TDefinition>(std::move(operation));
        return true;
    }

    template<typename TDefinition>
    bool Read(LocalStateView<TDefinition>& view) const noexcept {
        return _registry.template Read<TDefinition>(view);
    }

    template<typename TDefinition>
    LocalStateRegistrationSnapshot Registration() const noexcept {
        return _registry.template Registration<TDefinition>();
    }

    /// <summary>Captures current authoritative State for subscription establishment/resynchronization without advancing revision.</summary>
    template<typename TDefinition>
    bool Snapshot(StateUpdate<StateValueType<TDefinition>>& update) const {
        std::lock_guard<System::Synchronization::RecursiveMutex> publicationLock(_publicationMutex);
        LocalStateView<TDefinition> view;
        if (!_registry.template Read<TDefinition>(view)) return false;
        if (view.Revision == 0 || !_origin) return false;

        update.Header.Origin = _origin;
        update.Header.TypeId = StateTypeIdOf<TDefinition>;
        update.Header.Epoch = view.Epoch;
        update.Header.Revision = view.Revision;
        try {
            update.Value = view.ValueRef();
        } catch (...) {
            return false;
        }
        return true;
    }
};

} // namespace State
} // namespace ESPressio
