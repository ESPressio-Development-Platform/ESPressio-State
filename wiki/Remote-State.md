# Remote State

`RemoteStateManager<TContract, TMaximumDevices>` stores the newest accepted State snapshots for remote devices with explicit capacity.

```cpp
RemoteStateManager<DeviceStateContract, 8> remoteState;
```

## Applying a snapshot

A transport applies a complete typed value atomically:

```cpp
remoteState.Apply<FrontCamera>(
    device,
    1,   // epoch
    42,  // revision
    cameraValue
);
```

Older revisions cannot overwrite newer State. A newer epoch represents a new origin lifetime.

## Stable reads

Read into a snapshot:

```cpp
RemoteStateSnapshot<FrontCamera::Value> snapshot;

if (remoteState.Read<FrontCamera>(device, snapshot) && snapshot.HasValue) {
    // snapshot.Value
    // snapshot.Revision
    // snapshot.Availability
}
```

The copy is intentional. It gives the caller a stable transactional view while another execution context may continue updating repository storage.

## Compound values are atomic

A compound value such as coordinates, gyroscope axes or camera status is replaced as one State operation. Consumers should not observe fields mixed from different revisions.

## Enumeration

Diagnostic enumeration copies the minimum stable snapshot required while holding the repository lock, then invokes caller code after releasing it. Preserve this lock boundary when extending repository traversal.