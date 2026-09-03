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
/// Publication notification is serialized per State definition. A same-State NotifyChanged made from inside
/// a publication callback never recursively enters observers. Instead, one externally-preferred immutable
/// latest snapshot is retained for that State and dispatched after the active notification returns. Additional
/// same-State changes before that deferred snapshot is dispatched replace it with the newest revision. This is
/// the bounded latest-fact coalescing permitted by State semantics; emitted revisions remain strictly ordered,
/// while applications requiring every transition must use Event/stream semantics.
/// </remarks>
template<typename TContract, std::size_t TMaximumObservers = 8>
class StatePublisher final {
    static_assert(TMaximumObservers > 0, "StatePublisher observer capacity must be non-zero");
    static constexpr auto ExternalPreferred = System::Memory::MemoryPolicy::ExternalPreferred;

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

    template<typename TDefinition>
    struct NotificationState final {
        using Update = StateUpdate<StateValueType<TDefinition>>;
        bool Dispatching = false;
        std::shared_ptr<Update> PendingLatest;
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
    void DispatchSerialized(StateUpdate<StateValueType<TDefinition>> update) {
        try {
            while (true) {
                auto observable = ObservableSnapshot();
                if (observable) observable->template Published<TDefinition>(update);

                std::shared_ptr<StateUpdate<StateValueType<TDefinition>>> pending;
                {
                    std::lock_guard<System::Synchronization::RecursiveMutex> lock(_publicationMutex);
                    auto& state = GetNotificationState<TDefinition>();
                    pending = std::move(state.PendingLatest);
                    if (!pending) {
                        state.Dispatching = false;
                        return;
                    }
                }
                update = std::move(*pending);
            }
        } catch (...) {
            std::lock_guard<System::Synchronization::RecursiveMutex> lock(_publicationMutex);
            auto& state = GetNotificationState<TDefinition>();
            state.PendingLatest.reset();
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
    /// <remarks>User observers are invoked only after publisher/registry mutation locks have been released.</remarks>
    template<typename TDefinition>
    bool Bind(StateValueType<TDefinition>& source) {
        LocalStateRegistrationSnapshot registration{};
        {
            std::lock_guard<System::Synchronization::RecursiveMutex> publicationLock(_publicationMutex);
            if (!_origin) return false;
            if (!_registry.template Bind<TDefinition>(source)) return false;
            registration = _registry.template Registration<TDefinition>();
        }

        auto observable = ObservableSnapshot();
        if (observable) {
            const auto address = MakeStateAddress<TDefinition>(_origin);
            observable->SourceBound(address, registration.Epoch, registration.Revision);
            observable->AvailabilityChanged(address, UnboundStatus(), AvailableStatus());
        }
        return true;
    }

    /// <summary>Removes one bound source while preserving or discarding its registration lineage according to mode.</summary>
    /// <remarks>User observers are invoked only after publisher/registry mutation locks have been released.</remarks>
    template<typename TDefinition>
    bool Unbind(StateUnbindMode mode) {
        LocalStateRegistrationSnapshot before{};
        {
            std::lock_guard<System::Synchronization::RecursiveMutex> publicationLock(_publicationMutex);
            before = _registry.template Registration<TDefinition>();
            if (!before.Bound) return false;
            if (!_registry.template Unbind<TDefinition>(mode)) return false;
            GetNotificationState<TDefinition>().PendingLatest.reset();
        }

        auto observable = ObservableSnapshot();
        if (observable) {
            const auto address = MakeStateAddress<TDefinition>(_origin);
            observable->AvailabilityChanged(address, AvailableStatus(), UnboundStatus());
            observable->SourceUnbound(address, mode, before.Epoch, before.Revision);
        }
        return true;
    }

    /// <summary>Advances the local registration revision and synchronously publishes the current authoritative value.</summary>
    /// <remarks>
    /// Equality is deliberately not re-evaluated here: the caller has explicitly declared a semantic change.
    /// The source value is copied before the revision commit, so a failed value copy or deferred-storage
    /// allocation cannot consume a revision. Reentrant same-State notifications retain at most one latest
    /// immutable pending snapshot; later accepted changes replace it and therefore coalesce obsolete revisions.
    /// </remarks>
    template<typename TDefinition>
    bool NotifyChanged(StateRevision* committedRevision = nullptr) {
        using Update = StateUpdate<StateValueType<TDefinition>>;
        Update prepared;
        bool dispatchNow = false;
        StateRevision revision = 0;
        std::shared_ptr<Update> deferred;

        {
            std::lock_guard<System::Synchronization::RecursiveMutex> publicationLock(_publicationMutex);
            auto& notificationState = GetNotificationState<TDefinition>();

            LocalStateView<TDefinition> before;
            if (!_registry.template Read<TDefinition>(before)) return false;

            prepared.Header.Origin = _origin;
            prepared.Header.TypeId = StateTypeIdOf<TDefinition>;
            prepared.Header.Epoch = before.Epoch;
            try {
                prepared.Value = before.ValueRef();
            } catch (...) {
                return false;
            }

            if (notificationState.Dispatching) {
                try {
                    deferred = System::Memory::MakeShared<Update, ExternalPreferred>(prepared);
                } catch (...) {
                    return false;
                }
            }

            if (!_registry.template NotifyChanged<TDefinition>(revision)) return false;
            prepared.Header.Revision = revision;

            if (notificationState.Dispatching) {
                deferred->Header.Revision = revision;
                notificationState.PendingLatest = std::move(deferred);
            } else {
                notificationState.Dispatching = true;
                dispatchNow = true;
            }
        }

        if (committedRevision != nullptr) *committedRevision = revision;
        if (dispatchNow) DispatchSerialized<TDefinition>(std::move(prepared));
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

    /// <summary>Captures the current authoritative value for subscription establishment or resynchronization without advancing revision.</summary>
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
