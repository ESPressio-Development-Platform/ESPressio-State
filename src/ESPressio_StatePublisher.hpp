#pragma once

#include <cstddef>
#include <memory>
#include <mutex>
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
/// </remarks>
template<typename TContract, std::size_t TMaximumObservers = 8>
class StatePublisher final {
    static_assert(TMaximumObservers > 0, "StatePublisher observer capacity must be non-zero");

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

    DeviceIdentifier _origin{};
    LocalStateRegistry<TContract> _registry{};
    std::shared_ptr<PublisherObservable> _observable;
    mutable System::Synchronization::RecursiveMutex _observableMutex;

    std::shared_ptr<PublisherObservable> EnsureObservable() {
        std::lock_guard<System::Synchronization::RecursiveMutex> lock(_observableMutex);
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

    static StateAvailabilityStatus AvailableStatus() noexcept {
        return {StateAvailability::Available, StateAvailabilityReason::None};
    }

    static StateAvailabilityStatus UnboundStatus() noexcept {
        return {StateAvailability::Unavailable, StateAvailabilityReason::SourceUnbound};
    }

public:
    /// <summary>Maximum number of simultaneous observer registrations accepted by this publisher.</summary>
    static constexpr std::size_t MaximumObservers = TMaximumObservers;

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
    /// emitted to observers; the application-owned source remains the canonical local value.
    /// </remarks>
    template<typename TDefinition>
    bool NotifyChanged(StateRevision* committedRevision = nullptr) {
        StateRevision revision = 0;
        if (!_registry.template NotifyChanged<TDefinition>(revision)) return false;

        LocalStateView<TDefinition> view;
        if (!_registry.template Read<TDefinition>(view)) return false;
        if (view.Revision != revision) return false;

        StateUpdate<StateValueType<TDefinition>> update;
        update.Header.Origin = _origin;
        update.Header.TypeId = StateTypeIdOf<TDefinition>;
        update.Header.Epoch = view.Epoch;
        update.Header.Revision = view.Revision;
        update.Value = view.ValueRef();

        auto observable = ObservableSnapshot();
        if (observable) observable->template Published<TDefinition>(update);
        if (committedRevision != nullptr) *committedRevision = revision;
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
