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

void setup() {
    Serial.begin(115200);

    const uint8_t remoteMac[6] = {0x64, 0xB7, 0x08, 0x85, 0x63, 0x3D};
    const DeviceIdentifier remoteDevice = DeviceIdentifier::FromMacAddress(remoteMac);

    // In real use this value arrives from a State transport adapter.
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
