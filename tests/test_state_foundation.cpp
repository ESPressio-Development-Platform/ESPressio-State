#include <cassert>
#include <cstdint>

#include <ESPressio_State.hpp>

using namespace ESPressio::State;

struct GyroscopeData {
    int16_t X = 0;
    int16_t Y = 0;
    int16_t Z = 0;

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

struct LedState {
    using Value = bool;
    static constexpr StateTypeId Id = 0x1003;
};

using Contract = StateContract<FrontGyroscope, RearGyroscope, LedState>;

int main() {
    static_assert(Contract::StateCount == 3);
    static_assert(Contract::Contains<FrontGyroscope>);
    static_assert(Contract::IndexOf<FrontGyroscope>() == 0);
    static_assert(Contract::IndexOf<RearGyroscope>() == 1);
    static_assert(Contract::IndexOf<LedState>() == 2);

    const uint8_t mac[6] = {0x64, 0xB7, 0x08, 0x85, 0x63, 0x3D};
    const auto device = DeviceIdentifier::FromMacAddress(mac);
    assert(!device.IsZero());
    assert(device.Bytes()[10] == mac[0]);
    assert(device.Bytes()[15] == mac[5]);

    RemoteStateManager<Contract, 2> manager;
    assert(manager.GetDeviceCount() == 0);

    assert(manager.Apply<FrontGyroscope>(device, 1, 1, {1, 2, 3}));
    assert(manager.GetDeviceCount() == 1);

    auto* front = manager.Get<FrontGyroscope>(device);
    assert(front != nullptr);
    assert(front->HasValue);
    assert(front->Value == GyroscopeData{1, 2, 3});

    // Same payload type, different compile-time State identity and storage.
    assert(manager.Apply<RearGyroscope>(device, 1, 1, {4, 5, 6}));
    auto* rear = manager.Get<RearGyroscope>(device);
    assert(rear != nullptr);
    assert(rear->Value == GyroscopeData{4, 5, 6});
    assert(front->Value == GyroscopeData{1, 2, 3});

    // Stale and duplicate revisions never replace the latest State.
    assert(!manager.Apply<FrontGyroscope>(device, 1, 1, {9, 9, 9}));
    assert(!manager.Apply<FrontGyroscope>(device, 1, 0, {9, 9, 9}));
    assert(front->Value == GyroscopeData{1, 2, 3});

    assert(manager.Apply<FrontGyroscope>(device, 1, 2, {7, 8, 9}));
    assert(front->Revision == 2);
    assert(front->Value == GyroscopeData{7, 8, 9});

    // A newer epoch establishes a new authoritative State lifetime.
    assert(manager.Apply<FrontGyroscope>(device, 2, 1, {10, 11, 12}));
    assert(front->Epoch == 2);
    assert(front->Revision == 1);

    assert(manager.SetAvailability(device, RemoteDeviceAvailability::Connected));
    assert(manager.GetAvailability(device) == RemoteDeviceAvailability::Connected);

    return 0;
}
