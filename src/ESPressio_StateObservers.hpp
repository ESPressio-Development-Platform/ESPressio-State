#pragma once

#include <cstdint>

#include <ESPressio_IObserver.hpp>

#include "ESPressio_DeviceIdentifier.hpp"
#include "ESPressio_StateContract.hpp"

namespace ESPressio {
namespace State {

enum class RemoteDeviceAvailability : uint8_t;
enum class StateSubscriptionScope : uint8_t;
template<typename TValue> struct StateUpdate;

/// <summary>Observes registration, acceptance, rejection, and availability events from a remote-state manager.</summary>
class IRemoteStateManagerObserver : public Observable::IObserver {
public:
    virtual ~IRemoteStateManagerObserver() = default;
    /// <summary>Called when a previously unknown remote device is registered.</summary>
    virtual void OnRemoteStateDeviceRegistered(const DeviceIdentifier&) {}
    /// <summary>Called when an inbound remote-state revision is accepted.</summary>
    virtual void OnRemoteStateAccepted(const DeviceIdentifier&, StateTypeId, StateEpoch, StateRevision, bool) {}
    /// <summary>Called when an inbound remote-state revision is rejected as unsuitable or stale.</summary>
    virtual void OnRemoteStateRejected(const DeviceIdentifier&, StateTypeId, StateEpoch, StateRevision) {}
    /// <summary>Called when the known availability of a remote device changes.</summary>
    virtual void OnRemoteStateAvailabilityChanged(const DeviceIdentifier&, RemoteDeviceAvailability, RemoteDeviceAvailability) {}
};

/// <summary>Observes typed value changes for one remote state definition.</summary>
/// <typeparam name="TDefinition">State definition to observe.</typeparam>
template<typename TDefinition>
class IRemoteStateObserver : public Observable::IObserver {
public:
    /// <summary>Value type represented by the observed state definition.</summary>
    using Value = StateValueType<TDefinition>;
    virtual ~IRemoteStateObserver() = default;
    /// <summary>Called when a meaningful remote-state value change is accepted.</summary>
    virtual void OnRemoteStateChanged(
        StateTag<TDefinition>,
        const DeviceIdentifier&,
        bool hasPreviousValue,
        const Value& previousValue,
        const Value& latestValue,
        StateEpoch,
        StateRevision,
        RemoteDeviceAvailability
    ) {}
};

/// <summary>Observes availability transitions for remote devices.</summary>
class IRemoteDeviceAvailabilityObserver : public Observable::IObserver {
public:
    virtual ~IRemoteDeviceAvailabilityObserver() = default;
    /// <summary>Called when a remote device changes availability state.</summary>
    virtual void OnRemoteDeviceAvailabilityChanged(const DeviceIdentifier&, RemoteDeviceAvailability, RemoteDeviceAvailability) {}
};

/// <summary>Observes changes and capacity failures in the local state-subscription registry.</summary>
class IStateSubscriptionRegistryObserver : public Observable::IObserver {
public:
    virtual ~IStateSubscriptionRegistryObserver() = default;
    /// <summary>Called when a state subscription is added.</summary>
    virtual void OnStateSubscribed(StateTypeId, StateSubscriptionScope, const DeviceIdentifier&) {}
    /// <summary>Called when a state subscription is removed.</summary>
    virtual void OnStateUnsubscribed(StateTypeId, StateSubscriptionScope, const DeviceIdentifier&) {}
    /// <summary>Called when a subscription cannot be retained because registry capacity is exhausted.</summary>
    virtual void OnStateSubscriptionCapacityExhausted(StateTypeId, StateSubscriptionScope, const DeviceIdentifier&) {}
};

/// <summary>Observes changes and capacity failures in the remote-subscriber registry.</summary>
class IStateSubscriberRegistryObserver : public Observable::IObserver {
public:
    virtual ~IStateSubscriberRegistryObserver() = default;
    /// <summary>Called when a remote device subscribes to a state type.</summary>
    virtual void OnRemoteStateSubscriberAdded(const DeviceIdentifier&, StateTypeId) {}
    /// <summary>Called when a remote device unsubscribes from a state type.</summary>
    virtual void OnRemoteStateSubscriberRemoved(const DeviceIdentifier&, StateTypeId) {}
    /// <summary>Called when all subscriber entries associated with a remote device are removed.</summary>
    virtual void OnRemoteStateSubscriberDeviceRemoved(const DeviceIdentifier&) {}
    /// <summary>Called when a subscriber cannot be retained because registry capacity is exhausted.</summary>
    virtual void OnRemoteStateSubscriberCapacityExhausted(const DeviceIdentifier&, StateTypeId) {}
};

/// <summary>Observes state-source registration and publication lifecycle events.</summary>
class IStatePublisherObserver : public Observable::IObserver {
public:
    virtual ~IStatePublisherObserver() = default;
    /// <summary>Called when a state source is registered with the publisher.</summary>
    virtual void OnStateSourceRegistered(StateTypeId) {}
    /// <summary>Called when a state source is unregistered from the publisher.</summary>
    virtual void OnStateSourceUnregistered(StateTypeId) {}
    /// <summary>Called when a state revision is published.</summary>
    virtual void OnStatePublished(StateTypeId, StateEpoch, StateRevision) {}
};

/// <summary>Observes published updates for one strongly typed state definition.</summary>
/// <typeparam name="TDefinition">State definition to observe.</typeparam>
template<typename TDefinition>
class IStatePublishedObserver : public Observable::IObserver {
public:
    /// <summary>Value type represented by the observed state definition.</summary>
    using Value = StateValueType<TDefinition>;
    virtual ~IStatePublishedObserver() = default;
    /// <summary>Called when an update for this state definition is published.</summary>
    virtual void OnStatePublished(
        StateTag<TDefinition>,
        const StateUpdate<Value>&
    ) {}
};

/// <summary>Forwards a typed publication callback into a derived observer's templated handler.</summary>
/// <typeparam name="TDerived">Derived observer implementing <c>OnTypedStatePublished</c>.</typeparam>
/// <typeparam name="TDefinition">State definition forwarded by this base.</typeparam>
template<typename TDerived, typename TDefinition>
class StatePublishedObserverForwarder : public IStatePublishedObserver<TDefinition> {
public:
    /// <inheritdoc/>
    void OnStatePublished(
        StateTag<TDefinition>,
        const StateUpdate<StateValueType<TDefinition>>& update
    ) final {
        static_cast<TDerived*>(this)->template OnTypedStatePublished<TDefinition>(update);
    }
};

/// <summary>Aggregates typed publication observer bases for every definition in a state contract.</summary>
template<typename TDerived, typename TContract>
class StatePublishedObserverPack;

/// <summary>State-contract specialization that inherits one typed publication forwarder per state definition.</summary>
template<typename TDerived, typename... TDefinitions>
class StatePublishedObserverPack<TDerived, StateContract<TDefinitions...>> :
    public StatePublishedObserverForwarder<TDerived, TDefinitions>... {
};

/// <summary>Observes per-device state-publication acknowledgement lifecycle events.</summary>
class IStatePublicationObserver : public Observable::IObserver {
public:
    virtual ~IStatePublicationObserver() = default;
    /// <summary>Called when a state revision is awaiting remote acknowledgement.</summary>
    virtual void OnStatePublicationPending(const DeviceIdentifier&, StateTypeId, StateEpoch, StateRevision) {}
    /// <summary>Called when a newer state revision supersedes an unacknowledged publication.</summary>
    virtual void OnStatePublicationSuperseded(const DeviceIdentifier&, StateTypeId, StateEpoch, StateRevision, StateEpoch, StateRevision) {}
    /// <summary>Called when a pending state publication is acknowledged.</summary>
    virtual void OnStatePublicationAcknowledged(const DeviceIdentifier&, StateTypeId, StateEpoch, StateRevision) {}
    /// <summary>Called when an acknowledgement refers to a publication that is no longer current.</summary>
    virtual void OnStatePublicationStaleAcknowledgement(const DeviceIdentifier&, StateTypeId, StateEpoch, StateRevision) {}
};

}
}
