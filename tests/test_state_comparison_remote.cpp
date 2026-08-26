#include <cassert>
#include <cstdint>

#include <ESPressio_State.hpp>

using namespace ESPressio::State;

struct NoisyValue {
    int32_t Value = 0;
    bool operator==(const NoisyValue& other) const { return Value == other.Value; }
};

struct DeadbandState {
    using Value = NoisyValue;
    static constexpr StateTypeId Id = 0x9010;
};

namespace ESPressio {
namespace State {
template<>
struct StateComparison<DeadbandState> {
    using Value = StateValueType<DeadbandState>;
    static bool Equals(const Value& previous, const Value& current) {
        const auto difference = current.Value - previous.Value;
        return difference >= -5 && difference <= 5;
    }
};
} // namespace State
} // namespace ESPressio

class Observer final : public IRemoteStateManagerObserver {
public:
    int Accepted = 0;
    int Changed = 0;
    void OnRemoteStateDeviceRegistered(const DeviceIdentifier&) override {}
    void OnRemoteStateAccepted(const DeviceIdentifier&, StateTypeId, StateEpoch, StateRevision, bool changed) override {
        ++Accepted;
        if (changed) ++Changed;
    }
    void OnRemoteStateRejected(const DeviceIdentifier&, StateTypeId, StateEpoch, StateRevision) override {}
    void OnRemoteStateAvailabilityChanged(const DeviceIdentifier&, RemoteDeviceAvailability, RemoteDeviceAvailability) override {}
};

int main() {
    using Contract = StateContract<DeadbandState>;
    const uint8_t mac[6] = {0x02,0x00,0x00,0x00,0x00,0x01};
    const auto device = DeviceIdentifier::FromMacAddress(mac);

    RemoteStateManager<Contract, 1> manager;
    Observer observer;
    auto handle = manager.RegisterObserver(static_cast<IRemoteStateManagerObserver*>(&observer));

    assert(manager.Apply<DeadbandState>(device, 1, 1, {100}));
    assert(manager.Apply<DeadbandState>(device, 1, 2, {104}));
    assert(manager.Apply<DeadbandState>(device, 1, 3, {110}));

    // The policy belongs to the source/publication decision. Every distinct
    // newer revision arriving remotely is therefore an origin-declared change.
    assert(observer.Accepted == 3);
    assert(observer.Changed == 3);

    RemoteStateSnapshot<NoisyValue> snapshot;
    assert(manager.Read<DeadbandState>(device, snapshot));
    assert(snapshot.Revision == 3);
    assert(snapshot.Value.Value == 110);
    return 0;
}
