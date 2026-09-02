# State Contracts and Identity

Each logical State is represented by a compile-time definition containing its value type and stable `StateTypeId`.

```cpp
struct FrontGyroscope {
    using Value = GyroscopeData;
    static constexpr StateTypeId Id = 0x1001;
};
```

Combine definitions into a `StateContract<...>` for a device/domain.

## Identity is not the payload type

Two definitions may intentionally share the same `Value` type while representing different facts. `StateTag<TDefinition>` preserves this distinction in typed callbacks and repository access.

Do not identify core State by runtime strings.

## `DeviceIdentifier`

Devices use a transport-neutral 128-bit identity. Transport-specific identities can be mapped into it without leaking transport types into State.

For MAC-addressed devices:

```cpp
uint8_t mac[6] = {0x64, 0xB7, 0x08, 0x85, 0x63, 0x3D};
DeviceIdentifier device = DeviceIdentifier::FromMacAddress(mac);
```

## Optional names

A definition may expose a human-readable diagnostic `Name`, but the name never participates in core identity or wire routing. `StateTypeId` remains authoritative.

## Contract design guidance

Prefer State definitions that represent coherent atomic facts. If several fields must always be observed from the same revision, place them in one compound value rather than publishing each field independently.