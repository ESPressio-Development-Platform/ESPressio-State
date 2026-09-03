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
    virtual void OnRemoteStateDeviceRegistered(const DeviceIdentifier&) {}
    virtual void OnRemoteStateAccepted(const DeviceIdentifier&, StateTypeId, StateEpoch, StateRevision, bool) {}
    virtual void OnRemoteStateRejected(const DeviceIdentifier&, StateTypeId, StateEpoch, StateRevision) {}
    virtual void OnRemoteStateAvailabilityChanged(const DeviceIdentifier&, RemoteDeviceAvailability, RemoteDeviceAvailability) {}
};

template<typename TDefinition>
class IRemoteStateObserver : public Observable::IObserver {
public:
    using Value = StateValueType<TDefinition>;
    virtual ~IRemoteStateObserver() = default;
    virtual void OnRemoteStateChanged(StateTag<TDefinition>, const DeviceIdentifier&, bool, const Value&, const Value&, StateEpoch, StateRevision, RemoteDeviceAvailability) {}
};

class IRemoteDeviceAvailabilityObserver : public Observable::IObserver {
public:
    virtual ~IRemoteDeviceAvailabilityObserver() = default;
    virtual void OnRemoteDeviceAvailabilityChanged(const DeviceIdentifier&, RemoteDeviceAvailability, RemoteDeviceAvailability) {}
};

class IStateSubscriptionRegistryObserver : public Observable::IObserver {
public:
    virtual ~IStateSubscriptionRegistryObserver() = default;
    virtual void OnStateSubscribed(StateTypeId, StateSubscriptionScope, const DeviceIdentifier&) {}
    virtual void OnStateUnsubscribed(StateTypeId, StateSubscriptionScope, const DeviceIdentifier&) {}
    virtual void OnStateSubscriptionCapacityExhausted(StateTypeId, StateSubscriptionScope, const DeviceIdentifier&) {}
};

class IStateSubscriberRegistryObserver : public Observable::IObserver {
public:
    virtual ~IStateSubscriberRegistryObserver() = default;
    virtual void OnRemoteStateSubscriberAdded(const DeviceIdentifier&, StateTypeId) {}
    virtual void OnRemoteStateSubscriberRemoved(const DeviceIdentifier&, StateTypeId) {}
    virtual void OnRemoteStateSubscriberDeviceRemoved(const DeviceIdentifier&) {}
    virtual void OnRemoteStateSubscriberCapacityExhausted(const DeviceIdentifier&, StateTypeId) {}
};

class IStatePublisherObserver : public Observable::IObserver {
public:
    virtual ~IStatePublisherObserver() = default;
    virtual void OnStateSourceRegistered(StateTypeId) {}
    virtual void OnStateSourceUnregistered(StateTypeId) {}
    virtual void OnStatePublished(StateTypeId, StateEpoch, StateRevision) {}
};

template<typename TDefinition>
class IStatePublishedObserver : public Observable::IObserver {
public:
    using Value = StateValueType<TDefinition>;
    virtual ~IStatePublishedObserver() = default;
    virtual void OnStatePublished(StateTag<TDefinition>, const StateUpdate<Value>&) {}
};

template<typename TDerived, typename TDefinition>
class StatePublishedObserverForwarder : public IStatePublishedObserver<TDefinition> {
public:
    void OnStatePublished(StateTag<TDefinition>, const StateUpdate<StateValueType<TDefinition>>& update) final {
        static_cast<TDerived*>(this)->template OnTypedStatePublished<TDefinition>(update);
    }
};

template<typename TDerived, typename TContract>
class StatePublishedObserverPack;

template<typename TDerived, typename... TDefinitions>
class StatePublishedObserverPack<TDerived, StateContract<TDefinitions...>> :
    public StatePublishedObserverForwarder<TDerived, TDefinitions>... {
};

} // namespace State
} // namespace ESPressio
