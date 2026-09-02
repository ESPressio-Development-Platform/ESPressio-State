# ESPressio State

Strongly typed distributed State infrastructure for the ESPressio Development Platform.

ESPressio State communicates **what is true now**. It is deliberately distinct from Command execution and Event history: obsolete intermediate State is disposable, while the latest authoritative value matters.

## Current Version — 0.1.0

`0.1.0` remains the active development version on `feature/1-state-foundation`. No release/version cascade is being performed while this architecture is being completed.

The current branch includes:

- transport-neutral 128-bit `DeviceIdentifier` values;
- compiler-backed `StateContract<...>` definitions and `StateTag<TDefinition>` identities;
- fixed-capacity, typed `RemoteStateManager` repositories;
- source-backed `StatePublisher` publication without a duplicate local State repository;
- fixed-capacity local subscription and remote-subscriber registries;
- epoch/revision ordering and latest-only publication tracking;
- compact transport-neutral update, ACK, subscription, resynchronisation and disconnect messages;
- synchronous ESPressio Observable lifecycle notifications throughout the State components;
- an optional ESPressio Threads-backed coalesced application-observer execution layer;
- optional allocation-free State introspection and bounded diagnostic serialization;
- an optional ESP-NOW transport adapter in ESPressio-ESP-Now;
- optional State logging observers in ESPressio-Serial.

# Semantics

```text
Command
    "Please do this."

Event
    "This happened."

State
    "This is true now."
```

A State publisher may move rapidly through revisions:

```text
v10 -> v11 -> v12 -> v13
```

A consumer that has not yet processed those changes normally needs `v13`, not a historical queue containing `v10`, `v11`, and `v12`. ESPressio State therefore permits pending work and observer notifications to be **coalesced around the newest revision**.

If every transition must be preserved, use Event. If another component must perform an operation, use Command.

# Authoritative local State remains locally owned

State does not require a `LocalStateManager` and does not require application/domain objects to relocate their canonical data.

```text
CameraController
    owns authoritative camera data
             |
             | typed snapshot accessor
             v
       StatePublisher
             |
             v
          transport
```

For example:

```cpp
StatePublisher<DeviceStateContract> publisher(localDevice);

publisher.RegisterSource<FrontCamera>([&camera] {
    return camera.GetRemoteStateSnapshot();
});
```

When the owner changes:

```cpp
publisher.Publish<FrontCamera>();
```

The publisher asks the owner for the current snapshot and advances the State revision. `Snapshot<TDefinition>()` similarly obtains a fresh source-backed value for initial synchronisation or resynchronisation.

A caller may also publish an explicit typed value:

```cpp
publisher.Publish<FrontCamera>(currentValue);
```

# State definitions and contracts

Each logical State has an independent compile-time definition:

```cpp
struct GyroscopeData {
    float X;
    float Y;
    float Z;
};

struct FrontGyroscope {
    using Value = GyroscopeData;
    static constexpr ESPressio::State::StateTypeId Id = 0x1001;
};

struct RearGyroscope {
    using Value = GyroscopeData;
    static constexpr ESPressio::State::StateTypeId Id = 0x1002;
};

using DeviceStateContract = ESPressio::State::StateContract<
    FrontGyroscope,
    RearGyroscope
>;
```

`FrontGyroscope` and `RearGyroscope` deliberately share a payload type but remain different State identities. `StateTag<TDefinition>` preserves that distinction in typed observer callbacks as well as repository access.

No runtime string key is required by the core State path.

# Device identity

`DeviceIdentifier` is a transport-neutral 128-bit identity.

For MAC-addressed devices:

```cpp
uint8_t mac[6] = {0x64, 0xB7, 0x08, 0x85, 0x63, 0x3D};
DeviceIdentifier device = DeviceIdentifier::FromMacAddress(mac);
```

The mapping retains the MAC address reversibly while keeping ESP-NOW/WiFi types outside the State API.

# Remote State repository

Remote State capacity is explicit and deterministic:

```cpp
RemoteStateManager<DeviceStateContract, 8> remoteState;
```

A transport applies a complete typed snapshot atomically:

```cpp
remoteState.Apply<FrontCamera>(
    device,
    1,   // origin epoch
    42,  // revision
    cameraValue
);
```

Older revisions cannot overwrite newer State. A newer epoch represents a new origin lifetime, such as after a device restart.

## Thread-safe reads

Remote values are read into snapshots rather than by retaining pointers into repository storage:

```cpp
RemoteStateSnapshot<FrontCamera::Value> snapshot;

if (remoteState.Read<FrontCamera>(device, snapshot) && snapshot.HasValue) {
    const auto& value = snapshot.Value;
    const auto revision = snapshot.Revision;
    const auto availability = snapshot.Availability;
}
```

The copy is intentional: it provides a stable transactional view while the repository can continue to receive updates from another execution context.

For diagnostic/read-only enumeration, `ForEachDevice()` copies only device identity and availability while holding the repository lock, then executes callbacks after releasing it:

```cpp
remoteState.ForEachDevice([](const RemoteDeviceSnapshot& device) {
    // device.Identifier
    // device.Availability
});
```

# Compound State is atomic

A compound value such as gyroscope axes, coordinates, motor telemetry, or camera status is one logical snapshot.

```text
GyroscopeData
    X
    Y
    Z
```

The complete typed object is replaced as one repository operation. Consumers do not observe an unintended mixture of fields from different revisions.

# Subscriptions

`StateSubscriptionRegistry<TCapacity>` records **State this device wants to consume**.

```cpp
StateSubscriptionRegistry<8> subscriptions;

subscriptions.Subscribe<FrontGyroscope>();
subscriptions.Subscribe<RearGyroscope>(
    StateSubscription<RearGyroscope>::From(specificDevice)
);
```

Subscriptions may target:

```text
any device + one State definition
one device + one State definition
```

Both typed and transport-facing queries are available:

```cpp
subscriptions.IsSubscribed<FrontGyroscope>(device);
subscriptions.IsSubscribed(device, StateTypeIdOf<FrontGyroscope>);
```

A transport is expected to reject unsolicited State. Receiving a valid wire message is not sufficient permission to mutate `RemoteStateManager`; the `(origin, StateTypeId)` must match a local subscription.

`StateSubscriberRegistry<TContract, TMaximumSubscribers>` represents the opposite direction: remote devices that have subscribed to State owned by this device. This allows publishers/transports to send only State for which a consumer has declared interest.

# Acknowledgements and latest-only reliability

A State acknowledgement means:

> The receiving remote State repository accepted this revision, or already contains that exact revision.

It does **not** mean arbitrary application observers have finished reacting.

Pending delivery is keyed by destination and State identity. A newer revision supersedes the older pending value instead of adding another historical work item:

```text
revision 42 pending
        |
revision 43 published
        |
        v
pending slot now represents revision 43
```

An ACK for revision 42 cannot clear a newer pending revision 43.

# Availability and resynchronisation

Availability is metadata associated with a remote device, independent of whether the last known State remains readable:

```text
Unknown
Connected
Stale
Disconnected
ConnectionLost
```

The last accepted State may remain available while liveness changes, allowing application policy to decide whether stale/disconnected values remain useful.

Resynchronisation asks the authoritative owner for current source-backed snapshots and is scoped to subscribed State rather than dumping all State indiscriminately.

# Observation with ESPressio Observable

ESPressio Observable is the one mandatory ESPressio dependency of the State core.

State uses `ThreadSafeObservable` for lightweight synchronous lifecycle notification wherever a State component owns a meaningful transition. Current observer contracts cover:

- remote device registration;
- accepted/rejected State revisions;
- remote availability transitions;
- local subscribe/unsubscribe/capacity events;
- remote subscriber add/remove/capacity events;
- source registration/unregistration;
- generic and typed publication;
- pending/superseded/acknowledged/stale-ACK publication lifecycle.

Typed publication and remote-State observation carry `StateTag<TDefinition>`, so two State definitions that share the same `Value` type remain independently observable.

Synchronous lifecycle observers should remain lightweight. They can execute in infrastructure/transport-related contexts.

# Optional coalesced application observers

Arbitrary application work should not run directly in a transport receive path. When ESPressio Threads is available, include:

```cpp
#include <ESPressio_RemoteStateObserverThread.hpp>
```

and use:

```cpp
RemoteStateObserverThread<DeviceStateContract, 8> observerThread(remoteState);
```

This layer listens cheaply to `RemoteStateManager`, records dirty `(device, StateType)` identities, and later reads the newest snapshot from its own ESPressio Threads execution context.

```text
application last received v9
v10 accepted
v11 accepted
v12 accepted
     |
     v
one dirty identity
     |
observer thread runs
     |
     v
callback previous=v9, latest=v12
```

It intentionally does not construct a queue of obsolete State values. The optional observer layer retains only the last value delivered to application observers for each `(device, StateType)`, allowing meaningful coalesced `previous -> latest` callbacks without adding State history to the core repository.

Typed observers can be registered without ambiguous multiple-`IObserver` base conversions:

```cpp
observerThread.RegisterStateObserver<FrontGyroscope>(&observer);
observerThread.RegisterAvailabilityObserver(&observer);
```

ESPressio Threads is **not** a mandatory `library.json` dependency of State; it is required only by code that selects this optional header.

# Transport-neutral State protocol

State owns compact transport-independent messages for:

```text
Update
Acknowledgement
Subscribe
Unsubscribe
Resynchronize
Disconnect
```

Wire State identity is based on `DeviceIdentifier`, `StateTypeId`, epoch and revision rather than human-readable State names.

`StateCodec<TDefinition>` provides the payload codec boundary. The default path supports allocation-free encoding for suitable trivially-copyable values; richer contracts can specialize the codec without making diagnostic serialization mandatory.

# Optional State introspection

Human-readable names and repository enumeration are deliberately excluded from `ESPressio_State.hpp`. Select them explicitly:

```cpp
#include <ESPressio_StateIntrospection.hpp>
```

A State definition may optionally expose diagnostic metadata:

```cpp
struct FrontGyroscope {
    using Value = GyroscopeData;
    static constexpr StateTypeId Id = 0x1001;
    static constexpr const char* Name = "gyroscope.front";
};
```

`Name` never participates in identity or transport. `StateTypeId` remains authoritative. Definitions without a `Name` remain fully usable and introspectable by ID.

The optional layer supports symbolic lookup:

```cpp
const char* name = nullptr;
StateIntrospection<DeviceStateContract>::TryGetName(0x1001, name);

StateTypeId typeId = 0;
StateIntrospection<DeviceStateContract>::TryGetTypeId("gyroscope.front", typeId);
```

and three typed repository scopes:

```cpp
// one State from one device
RemoteStateIntrospectionSnapshot<FrontGyroscope> snapshot;
StateIntrospection<DeviceStateContract>::Read<FrontGyroscope>(
    remoteState,
    device,
    snapshot
);

// all currently-valued State for one device
StateIntrospection<DeviceStateContract>::ForEachState(
    remoteState,
    device,
    [](const auto& state) { /* typed snapshot */ }
);

// all currently-valued remote State
StateIntrospection<DeviceStateContract>::ForEachRemoteState(
    remoteState,
    [](const auto& state) { /* typed snapshot */ }
);
```

Inclusion enables `ESPRESSIO_STATE_ENABLE_INTROSPECTION` by default. A build may define it as `0` before including the optional header to compile the feature out entirely.

# Optional diagnostic serialization

Diagnostic serialization is also explicit:

```cpp
#include <ESPressio_StateSerialization.hpp>
```

It produces a bounded `SerializedRemoteState<TDefinition>` record containing:

```text
DeviceIdentifier
StateTypeId
optional symbolic Name
Epoch
Revision
Availability
bounded encoded payload
```

The payload is produced by the existing authoritative `StateCodec<TDefinition>`. No second wire representation is introduced, and State does not gain a mandatory dependency on ESPressio Serializable, ArduinoJson, JSON, CBOR, Serial, or Web tooling.

Single State, runtime-selected State, one-device traversal and whole-repository traversal are supported:

```cpp
SerializedRemoteState<FrontGyroscope> record;
StateSerialization<DeviceStateContract>::Serialize<FrontGyroscope>(
    remoteState,
    device,
    record
);

StateSerialization<DeviceStateContract>::Serialize(
    remoteState,
    device,
    typeId,
    [](const auto& selected) { /* selected typed record */ }
);

StateSerialization<DeviceStateContract>::ForEachState(
    remoteState,
    device,
    [](const auto& state) { /* diagnostic adapter */ }
);

StateSerialization<DeviceStateContract>::ForEachRemoteState(
    remoteState,
    [](const auto& state) { /* Serial/Web/debug adapter */ }
);
```

Inclusion enables `ESPRESSIO_STATE_ENABLE_SERIALIZATION` by default. A build may define it as `0` to exclude this layer entirely. Core publication, repository storage, subscriptions, acknowledgements and transport do not include or depend on either optional diagnostic header.

# Optional ESP-NOW integration

ESPressio-ESP-Now currently contains the opt-in:

```cpp
#include <ESPressio_ESPNowStateTransport.hpp>
```

on its `main` branch.

The adapter maps ESP-NOW MAC peers to `DeviceIdentifier`, carries the State protocol, sends only subscribed State, rejects unsolicited incoming State, acknowledges repository acceptance, and retains at most the latest pending wire image for each `(subscriber, StateType)`.

ESPressio-ESP-Now does not acquire a mandatory ESPressio-State dependency merely by providing this adapter.

# Optional Serial logging observers

ESPressio-Serial currently contains the opt-in:

```cpp
#include <ESPressio_StateMonitor.hpp>
```

on its `main` branch.

`ESPressio::Serial::StateMonitor` can observe selected State lifecycle components and emit Serial diagnostics. It is not included by the normal `ESPressio_Serial.hpp` path, and ESPressio-Serial does not acquire a mandatory State dependency unless an implementing application explicitly selects the State monitor header.

This is intentional: Serial output can affect timing and remains developer opt-in, consistent with other ESPressio Serial observers.

# Dependencies

The active development package currently resolves:

```text
ESPressio Observable main
```

C++17 and RTTI are required by the current Observable observer model.

Not mandatory for the State core:

```text
ESPressio Threads
ESPressio Serial
ESPressio ESP-Now
ESPressio Serializable
ESPressio Event
```

Those dependencies are consumed only by optional integration layers that need them. The State introspection and bounded diagnostic serialization headers themselves do not require ESPressio Serializable.

# Installation during active development

```ini
lib_deps =
    https://github.com/ESPressio-Development-Platform/ESPressio-State.git#main
```

Arduino-ESP32 builds must select C++17 and RTTI explicitly when the framework supplies GNU++11 and `-fno-rtti` defaults:

```ini
build_unflags =
    -std=gnu++11
    -fno-rtti
build_flags =
    -std=gnu++17
    -frtti
```

# State vs Event vs Command

```text
State
    what is true now
    latest value matters
    stale intermediate values are disposable

Event
    something happened
    historical transitions may matter

Command
    something should be done
    execution/result semantics matter
```

State must not be used to recreate desired-state Command semantics.

# Examples

- `examples/TypedRemoteState` — two independent gyroscope State definitions sharing the same payload type, stored and read through a typed `RemoteStateManager`.

# Active feature issues

- #1 — State contracts and transport-neutral device identity
- #2 — Typed remote State storage
- #3 — State publication and subscription model
- #4 — State transport acknowledgements and coalescing
- #5 — Remote device availability and resynchronisation
- #6 — Remote State observation and coalesced execution
- #7 — Optional State introspection and serialization
- #8 — Transport adapter integration

Downstream integration work is additionally tracked in the owning libraries, including ESPressio-ESP-Now #48 and ESPressio-Serial #41.

All current State development remains on `feature/1-state-foundation`; `main` is not the active integration target during this development round.

## License

Licensed under the Apache License 2.0. See [LICENSE](LICENSE).
