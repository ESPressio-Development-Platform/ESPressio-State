# Getting Started

Begin by defining the logical State values that make up your device/application contract.

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

Although the two definitions share the same payload type, they remain different State identities.

## Publishing local State

Authoritative data stays with its domain owner. Register a source accessor with `StatePublisher` and publish when the owner changes:

```cpp
StatePublisher<DeviceStateContract> publisher(localDevice);

publisher.RegisterSource<FrontCamera>([&camera] {
    return camera.GetRemoteStateSnapshot();
});

publisher.Publish<FrontCamera>();
```

## Reading remote State

Create a repository with explicit capacity:

```cpp
RemoteStateManager<DeviceStateContract, 8> remoteState;
```

Read into a stable snapshot rather than retaining pointers into repository storage.

## Choose State for the right semantics

Use State when the latest truth matters. Use Event when every transition/history matters, and Command when another component must perform an operation.