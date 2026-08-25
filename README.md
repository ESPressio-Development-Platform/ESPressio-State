# ESPressio State

Strongly typed distributed State infrastructure for the ESPressio Development Platform.

ESPressio State provides a transport-neutral way for embedded devices to publish authoritative State snapshots and maintain typed replicas of subscribed State from remote devices without treating State as a Command or Event.

## Current Version — 0.1.0

State 0.1.0 is the initial mutable development release. The current working branch establishes transport-neutral device identity, compile-time State contracts, and a fixed-capacity typed `RemoteStateManager`. Publication, subscriptions, acknowledgements, availability, observation, optional introspection, and concrete transport adapters are tracked as separate feature issues and are being added incrementally on the same working branch.

# Why a State primitive?

Distributed applications frequently need to communicate **what is true now**, rather than instructing another device to perform an operation or preserving every historical transition.

```text
Command
    "Please do this."

Event
    "This happened."

State
    "This is true now."
```

State is deliberately **latest-value oriented**. If a value changes several times before a remote device consumes it, obsolete intermediate values do not need to be preserved merely for delivery completeness.

```text
Local changes
    v10 -> v11 -> v12 -> v13

Remote State needs
    latest authoritative value: v13
```

This allows State transport and storage to coalesce obsolete updates rather than behaving like a FIFO work queue.

## Authoritative local State remains locally owned

ESPressio State does **not** require an application to centralize or duplicate its authoritative local State.

A camera controller, sensor, motor driver, application service, or other domain object continues to own its State in the implementation that naturally owns that information.

```text
CameraController
    owns local implementation data
           |
           | produces a State snapshot
           v
    FrontCamera::Value
           |
           v
       State publisher
```

Only the shared State definition and its `Value` representation form the contract between devices.

## State definitions are compiler-backed contracts

A State definition identifies one logical State and declares the type used to represent it remotely:

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
```

`FrontGyroscope` and `RearGyroscope` use the same payload type but remain completely independent State identities.

No arbitrary runtime string key is required by the core State API.

## Compound State is atomic

State is not limited to scalar values. A compound object such as gyroscope axes, position coordinates, camera status, or motor telemetry is treated as one logical State snapshot.

Remote storage replaces the complete typed object as one operation, so observers never need to see an unintended mixture such as new X with old Y and old Z.

## Stale State is intentionally disposable

State is different from Event history.

Older revisions must never overwrite newer State, and future transport work will deliberately supersede stale pending updates when a newer authoritative value exists.

If every intermediate transition matters, use an Event. If execution matters, use a Command.

# ESPressio Development Platform

ESPressio libraries are designed to be light-weight, strongly typed, object-oriented, composable, and to follow SOLID dependency boundaries wherever practical on embedded C++ targets.

State follows those goals by keeping the core contract/storage layer transport-neutral and allocation-conscious while allowing concrete transports to live in the libraries that own those transports.

## License

Licensed under the Apache License 2.0. See [LICENSE](LICENSE).

# Namespace

```cpp
ESPressio::State
```

Important public concepts currently include:

- `DeviceIdentifier` — fixed 128-bit transport-neutral device identity.
- `StateTypeId`, `StateRevision`, and `StateEpoch`.
- `StateContract<...>` — compile-time collection of supported State definitions.
- `StateValueType<TDefinition>` — payload type associated with a State definition.
- `RemoteStateManager<TContract, TMaximumDevices>` — fixed-capacity typed repository of replicated remote State.
- `RemoteStateSlot<T>` — latest known value plus epoch/revision metadata.
- `RemoteDeviceAvailability` — availability metadata retained independently from State values.

# Dependencies

The current State foundation is dependency-free beyond C++17.

This is intentional. Transport-neutral State contracts and remote storage do not require Threads, Event, Observable, Serializable, ESP-NOW, WiFi, or Serial.

Later features will consume ESPressio dependencies only where their responsibilities genuinely require them.

# Installation

During active development, consume the working branch directly:

```ini
lib_deps =
    https://github.com/ESPressio-Development-Platform/ESPressio-State.git#feature/1-state-foundation
```

The current implementation requires C++17.

# Defining a State contract

Define the shared transport representation separately from the class that owns authoritative local State.

```cpp
#include <ESPressio_State.hpp>

using namespace ESPressio::State;

struct CameraStateData {
    bool Connected = false;
    float Zoom = 0.0f;
    float FocusDistance = 0.0f;
};

struct FrontCamera {
    using Value = CameraStateData;
    static constexpr StateTypeId Id = 0x2001;
};

struct RearCamera {
    using Value = CameraStateData;
    static constexpr StateTypeId Id = 0x2002;
};

using DeviceStateContract = StateContract<
    FrontCamera,
    RearCamera
>;
```

The contract automatically knows its number of State definitions:

```cpp
static_assert(DeviceStateContract::StateCount == 2);
```

The application does not separately specify a maximum number of State keys.

# Authoritative local storage does not need to use the State type

The origin device remains free to organize its domain implementation naturally:

```cpp
class CameraController {
private:
    bool _connected = false;
    float _zoom = 1.0f;
    float _focusDistance = 2.5f;

public:
    FrontCamera::Value GetRemoteStateSnapshot() const {
        return {
            _connected,
            _zoom,
            _focusDistance
        };
    }
};
```

The only shared contract is `FrontCamera` and `FrontCamera::Value`.

Future publication APIs will allow the authoritative owner to publish that typed snapshot when its State changes without relocating the owner's data into a central local repository.

# Creating a fixed-capacity Remote State repository

The maximum number of remote devices is an explicit compile-time decision:

```cpp
RemoteStateManager<DeviceStateContract, 8> remoteState;
```

The number and types of supported State values are derived from `DeviceStateContract`.

The repository therefore has deterministic device capacity without requiring string-key maps or runtime State registration.

# Device identity

`DeviceIdentifier` is always 128 bits and is independent of any transport.

For ESP32-class devices, an adapter can derive it automatically from the hardware MAC address:

```cpp
uint8_t mac[6] = {
    0x64, 0xB7, 0x08,
    0x85, 0x63, 0x3D
};

DeviceIdentifier device =
    DeviceIdentifier::FromMacAddress(mac);
```

Application developers do not need to hardcode a separate identifier for each ESP32 board when a stable hardware identity is available.

Other platforms can construct the same 128-bit abstraction from an appropriate stable identifier without introducing WiFi or ESP-NOW dependencies into State.

# Applying remote State

Transport adapters ultimately apply a typed State snapshot to the repository:

```cpp
remoteState.Apply<FrontCamera>(
    device,
    1,  // origin epoch/session
    42, // monotonically increasing State revision
    CameraStateData{
        true,
        2.0f,
        1.75f
    }
);
```

The complete `CameraStateData` object is committed as one State update.

Older or duplicate revisions are rejected:

```cpp
bool applied = remoteState.Apply<FrontCamera>(
    device,
    1,
    41,
    olderValue
);

// applied == false when revision 42 is already known.
```

A newer epoch establishes a new authoritative State lifetime after an origin device restarts.

# Reading typed Remote State

There is no string-based lookup in the foundational API:

```cpp
const auto* camera =
    remoteState.Get<FrontCamera>(device);

if (camera != nullptr && camera->HasValue) {
    const CameraStateData& value = camera->Value;

    // value is compiler-known to be CameraStateData.
}
```

The compiler verifies that `FrontCamera` belongs to the repository's `StateContract` and that its returned value type is `FrontCamera::Value`.

Multiple definitions using the same payload type remain independently addressable:

```cpp
const auto* front = remoteState.Get<FrontCamera>(device);
const auto* rear  = remoteState.Get<RearCamera>(device);
```

# Remote device availability

Availability is intentionally separate from the last known State value:

```cpp
remoteState.SetAvailability(
    device,
    RemoteDeviceAvailability::Connected
);
```

A future liveness integration may move the device through states such as:

```text
Connected
Stale
Disconnected
ConnectionLost
```

The last known replicated State can remain queryable even when the device is no longer considered currently available. Application code decides how to represent that condition.

# Planned subscription semantics

Subscriptions are declarations of interest, not additional copies of application State.

The design supports:

```text
all devices + one State definition
one device  + one State definition
runtime subscribe/unsubscribe within fixed capacity
static subscriptions declared before initialization
```

Only State a consumer has subscribed to should cross a transport.

Initial and reconnect synchronization will likewise be scoped to subscribed State rather than sending every State known by the origin.

See issue #3 and issue #5 for the active design work.

# Planned acknowledgement semantics

A State acknowledgement will mean:

> The receiving Remote State repository accepted this revision.

It will **not** mean that arbitrary application observers have finished reacting to it.

When a newer revision exists, stale pending updates and stale acknowledgement waits are superseded rather than preserved as historical work.

See issue #4.

# State vs Event vs Command

Use the primitives according to their semantics:

```text
State
    shares what is true now
    latest value matters
    stale intermediate values are disposable

Event
    reports that something happened
    historical transitions may matter

Command
    asks something to happen
    execution/result semantics may matter
```

Do not use State to reproduce desired-state Command semantics. If Device A wants Device B to perform an operation, ESPressio Command remains the appropriate primitive.

# Optional diagnostics and serialization

Human-readable names, string lookup, serialization, complete repository dumps, Serial diagnostics, or Web UI introspection must not impose mandatory runtime overhead on the core State system.

Those facilities are tracked as opt-in functionality under issue #7 and will be removable from builds that do not need them.

# Examples

- `examples/TypedRemoteState` — defines two gyroscope State identities sharing the same `GyroscopeData` value type and reads them through a typed `RemoteStateManager`.

# Active feature issues

- #1 — State contracts and transport-neutral device identity
- #2 — Typed remote state storage
- #3 — State publication and subscription model
- #4 — State transport acknowledgements and coalescing
- #5 — Remote device availability and resynchronisation
- #6 — Remote State observation and coalesced execution
- #7 — Optional State introspection and serialization
- #8 — Transport adapter integration for ESP-NOW and Serial

All current development remains on `feature/1-state-foundation`; `main` is not the active integration target during this development round.
