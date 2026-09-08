#pragma once

#include <cstdint>

#include <ESPressio_IObserver.hpp>

#include "ESPressio_DeviceIdentifier.hpp"
#include "ESPressio_StateAddress.hpp"
#include "ESPressio_StateAvailability.hpp"
#include "ESPressio_StateContract.hpp"

namespace ESPressio {
namespace State {

enum class StateSubscriptionScope : uint8_t;
enum class StateUnbindMode : uint8_t;
template<typename TValue> struct StateUpdate;

/// <summary>Observes registration, replica acceptance/rejection, State availability, and source reachability.</summary>
/**
 * ESPressio Memory Audit
 * Inherited Memory Total: 4 bytes [0 bytes dynamic allocation]
 * Members: none; polymorphic/virtual-base object metadata is included in the total.
 * Total Memory: 4 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
class IRemoteStateManagerObserver : public Observable::IObserver {
public:
    virtual ~IRemoteStateManagerObserver() = default;
    virtual void OnRemoteStateDeviceRegistered(const DeviceIdentifier&) {}
    virtual void OnRemoteStateAccepted(const DeviceIdentifier&, StateTypeId, StateEpoch, StateRevision, bool) {}
    virtual void OnRemoteStateRejected(const DeviceIdentifier&, StateTypeId, StateEpoch, StateRevision) {}
    virtual void OnRemoteStateAvailabilityChanged(const StateAddress&, StateAvailabilityStatus, StateAvailabilityStatus) {}
    virtual void OnRemoteStateReachabilityChanged(const DeviceIdentifier&, StateSourceReachability, StateSourceReachability) {}
};

/**
 * ESPressio Memory Audit
 * Inherited Memory Total: 4 bytes [0 bytes dynamic allocation]
 * Members: none; polymorphic/virtual-base object metadata is included in the total.
 * Total Memory: 4 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
template<typename TDefinition>
class IRemoteStateObserver : public Observable::IObserver {
public:
    using Value = StateValueType<TDefinition>;
    virtual ~IRemoteStateObserver() = default;
    virtual void OnRemoteStateChanged(
        StateTag<TDefinition>,
        const DeviceIdentifier&,
        bool,
        const Value&,
        const Value&,
        StateEpoch,
        StateRevision,
        StateAvailabilityStatus
    ) {}
};

/// <summary>Observes effective availability changes of remote State identities.</summary>
/**
 * ESPressio Memory Audit
 * Inherited Memory Total: 4 bytes [0 bytes dynamic allocation]
 * Members: none; polymorphic/virtual-base object metadata is included in the total.
 * Total Memory: 4 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
class IRemoteStateAvailabilityObserver : public Observable::IObserver {
public:
    virtual ~IRemoteStateAvailabilityObserver() = default;
    virtual void OnRemoteStateAvailabilityChanged(const StateAddress&, StateAvailabilityStatus, StateAvailabilityStatus) {}
};

/// <summary>Observes reachability changes for authoritative remote devices.</summary>
/**
 * ESPressio Memory Audit
 * Inherited Memory Total: 4 bytes [0 bytes dynamic allocation]
 * Members: none; polymorphic/virtual-base object metadata is included in the total.
 * Total Memory: 4 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
class IRemoteStateReachabilityObserver : public Observable::IObserver {
public:
    virtual ~IRemoteStateReachabilityObserver() = default;
    virtual void OnRemoteStateReachabilityChanged(const DeviceIdentifier&, StateSourceReachability, StateSourceReachability) {}
};

/**
 * ESPressio Memory Audit
 * Inherited Memory Total: 4 bytes [0 bytes dynamic allocation]
 * Members: none; polymorphic/virtual-base object metadata is included in the total.
 * Total Memory: 4 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
class IStateSubscriptionRegistryObserver : public Observable::IObserver {
public:
    virtual ~IStateSubscriptionRegistryObserver() = default;
    virtual void OnStateSubscribed(StateTypeId, StateSubscriptionScope, const DeviceIdentifier&) {}
    virtual void OnStateUnsubscribed(StateTypeId, StateSubscriptionScope, const DeviceIdentifier&) {}
    virtual void OnStateSubscriptionCapacityExhausted(StateTypeId, StateSubscriptionScope, const DeviceIdentifier&) {}
};

/**
 * ESPressio Memory Audit
 * Inherited Memory Total: 4 bytes [0 bytes dynamic allocation]
 * Members: none; polymorphic/virtual-base object metadata is included in the total.
 * Total Memory: 4 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
class IStateSubscriberRegistryObserver : public Observable::IObserver {
public:
    virtual ~IStateSubscriberRegistryObserver() = default;
    virtual void OnRemoteStateSubscriberAdded(const DeviceIdentifier&, StateTypeId) {}
    virtual void OnRemoteStateSubscriberRemoved(const DeviceIdentifier&, StateTypeId) {}
    virtual void OnRemoteStateSubscriberDeviceRemoved(const DeviceIdentifier&) {}
    virtual void OnRemoteStateSubscriberCapacityExhausted(const DeviceIdentifier&, StateTypeId) {}
};

/// <summary>Observes local authoritative State binding, availability and publication lifecycle.</summary>
/**
 * ESPressio Memory Audit
 * Inherited Memory Total: 4 bytes [0 bytes dynamic allocation]
 * Members: none; polymorphic/virtual-base object metadata is included in the total.
 * Total Memory: 4 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
class IStatePublisherObserver : public Observable::IObserver {
public:
    virtual ~IStatePublisherObserver() = default;

    /// <summary>Called after an application-owned source becomes the active local authority.</summary>
    virtual void OnStateSourceBound(const StateAddress&, StateEpoch, StateRevision) {}

    /// <summary>Called after the active local source is removed.</summary>
    virtual void OnStateSourceUnbound(const StateAddress&, StateUnbindMode, StateEpoch, StateRevision) {}

    /// <summary>Called when local authoritative availability changes independently of value publication.</summary>
    virtual void OnStateAvailabilityChanged(const StateAddress&, StateAvailabilityStatus, StateAvailabilityStatus) {}

    /// <summary>Called after an explicit authoritative change has committed a new State revision.</summary>
    virtual void OnStatePublished(const StateAddress&, StateEpoch, StateRevision) {}
};

/**
 * ESPressio Memory Audit
 * Inherited Memory Total: 4 bytes [0 bytes dynamic allocation]
 * Members: none; polymorphic/virtual-base object metadata is included in the total.
 * Total Memory: 4 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
template<typename TDefinition>
class IStatePublishedObserver : public Observable::IObserver {
public:
    using Value = StateValueType<TDefinition>;
    virtual ~IStatePublishedObserver() = default;
    virtual void OnStatePublished(StateTag<TDefinition>, const StateUpdate<Value>&) {}
};

/**
 * ESPressio Memory Audit
 * Inherited Memory Total: 4 bytes [0 bytes dynamic allocation]
 * Members: none (standalone empty object occupies 1 byte; an eligible empty base may be optimized to 0 bytes).
 * Total Memory: 4 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
template<typename TDerived, typename TDefinition>
class StatePublishedObserverForwarder : public IStatePublishedObserver<TDefinition> {
public:
    void OnStatePublished(StateTag<TDefinition>, const StateUpdate<StateValueType<TDefinition>>& update) final {
        static_cast<TDerived*>(this)->template OnTypedStatePublished<TDefinition>(update);
    }
};

template<typename TDerived, typename TContract>
class StatePublishedObserverPack;

/**
 * ESPressio Memory Audit
 * Inherited Memory Total: 4 bytes [0 bytes dynamic allocation]
 * Members: none (standalone empty object occupies 1 byte; an eligible empty base may be optimized to 0 bytes).
 * Total Memory: 4 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
template<typename TDerived, typename... TDefinitions>
class StatePublishedObserverPack<TDerived, StateContract<TDefinitions...>> :
    public StatePublishedObserverForwarder<TDerived, TDefinitions>... {
};

} // namespace State
} // namespace ESPressio
