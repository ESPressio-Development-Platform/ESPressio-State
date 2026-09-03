#include <ESPressio_State.hpp>

using namespace ESPressio::State;

struct GyroscopeData {
    float X = 0.0f;
    float Y = 0.0f;
    float Z = 0.0f;

    bool operator==(const GyroscopeData& other) const {
        return X == other.X && Y == other.Y && Z == other.Z;
    }
};

struct FrontGyroscope {
    using Value = GyroscopeData;
    static constexpr StateTypeId Id = 0x1001;
};

struct RearGyroscope {
    using Value = GyroscopeData;
    static constexpr StateTypeId Id = 0x1002;
};

using RemoteContract = StateContract<FrontGyroscope, RearGyroscope>;
RemoteStateManager<RemoteContract, 4> remoteState;

static DeviceIdentifier ExampleRemoteDevice() {
    // DeviceIdentifier is the permanent platform identity imported from
    // ESPressio-System. Transport adapters obtain/validate this identity from
    // their authenticated context rather than deriving it inside State.
    DeviceIdentifier::Storage bytes{};
    bytes[15] = 0x42;
    return DeviceIdentifier(bytes);
}

void setup() {
    Serial.begin(115200);

    const DeviceIdentifier remoteDevice = ExampleRemoteDevice();

    // In real use, reachability is supplied by the active transport/Mesh
    // integration independently of the source-authoritative State value.
    remoteState.SetReachability(
        remoteDevice,
        StateSourceReachability::Reachable
    );

    // In real use this authoritative publication arrives from a State
    // transport adapter after source/subscription validation.
    remoteState.Apply<FrontGyroscope>(
        remoteDevice,
        1,
        1,
        GyroscopeData{1.0f, 2.0f, 3.0f}
    );

    RemoteStateSnapshot<GyroscopeData> front;
    if (
        remoteState.Read<FrontGyroscope>(remoteDevice, front) &&
        front.HasValue
    ) {
        Serial.printf(
            "Front gyro: %.2f %.2f %.2f\n",
            front.Value.X,
            front.Value.Y,
            front.Value.Z
        );
    }
}

void loop() {
}
