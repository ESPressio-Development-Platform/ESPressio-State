#pragma once

#include <cstdint>

#include <ESPressio_IObserver.hpp>

#include "ESPressio_DeviceIdentifier.hpp"
#include "ESPressio_StateContract.hpp"

namespace ESPressio {
namespace State {

enum class RemoteDeviceAvailability : uint8_t;
enum class StateSubscriptionScope : uint8_t;

class IRemoteStateManagerObserver : public Observable::IObserver {
public:
    virtual ~IRemoteStateManagerObserver() = default;

    virtual void OnRemoteStateDeviceRegistered(
        const DeviceIdentifier&
    ) {}

    virtual void OnRemoteStateAccepted(
        const DeviceIdentifier&,
        StateTypeId,
        StateEpoch,
        StateRevision,
        bool
    ) {}

    virtual void OnRemoteStateRejected(
        const DeviceIdentifier&,
        StateTypeId,
        StateEpoch,
        StateRevision
    ) {}

    virtual void OnRemoteStateAvailabilityChanged(
        const DeviceIdentifier&,
        RemoteDeviceAvailability,
        RemoteDeviceAvailability
    ) {}
};

template<typename TDefinition>
class IRemoteStateObserver : public Observable::IObserver {
public:
    using Value = StateValueType<TDefinition>;

    virtual ~IRemoteStateObserver() = default;

    virtual void OnRemoteStateChanged(
        const DeviceIdentifier&,
        const Value&,
        StateEpoch,
        StateRevision,
        RemoteDeviceAvailability
    ) {}
};

class IRemoteDeviceAvailabilityObserver : public Observable::IObserver {
public:
    virtual ~IRemoteDeviceAvailabilityObserver() = default;

    virtual void OnRemoteDeviceAvailabilityChanged(
        const DeviceIdentifier&,
        RemoteDeviceAvailability,
        RemoteDeviceAvailability
    ) {}
};

class IStateSubscriptionRegistryObserver : public Observable::IObserver {
public:
    virtual ~IStateSubscriptionRegistryObserver() = default;

    virtual void OnStateSubscribed(
        StateTypeId,
        StateSubscriptionScope,
        const DeviceIdentifier&
    ) {}

    virtual void OnStateUnsubscribed(
        StateTypeId,
        StateSubscriptionScope,
        const DeviceIdentifier&
    ) {}

    virtual void OnStateSubscriptionCapacityExhausted(
        StateTypeId,
        StateSubscriptionScope,
        const DeviceIdentifier&
    ) {}
};

class IStatePublicationObserver : public Observable::IObserver {
public:
    virtual ~IStatePublicationObserver() = default;

    virtual void OnStatePublicationPending(
        const DeviceIdentifier&,
        StateTypeId,
        StateEpoch,
        StateRevision
    ) {}

    virtual void OnStatePublicationSuperseded(
        const DeviceIdentifier&,
        StateTypeId,
        StateEpoch,
        StateRevision,
        StateEpoch,
        StateRevision
    ) {}

    virtual void OnStatePublicationAcknowledged(
        const DeviceIdentifier&,
        StateTypeId,
        StateEpoch,
        StateRevision
    ) {}

    virtual void OnStatePublicationStaleAcknowledgement(
        const DeviceIdentifier&,
        StateTypeId,
        StateEpoch,
        StateRevision
    ) {}
};

}
}
