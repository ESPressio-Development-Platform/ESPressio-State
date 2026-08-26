#include <cassert>
#include <cstdint>
#include <cstdlib>

#include <ESPressio_State.hpp>

using namespace ESPressio::State;

struct AnalogValue {
    int32_t Value = 0;
    bool operator==(const AnalogValue& other) const { return Value == other.Value; }
};

struct ExactAnalogState {
    using Value = AnalogValue;
    static constexpr StateTypeId Id = 0x9001;
};

struct DeadbandAnalogState {
    using Value = AnalogValue;
    static constexpr StateTypeId Id = 0x9002;
};

namespace ESPressio {
namespace State {

template<>
struct StateComparison<DeadbandAnalogState> {
    using Value = StateValueType<DeadbandAnalogState>;

    static bool Equals(const Value& previous, const Value& current) {
        return std::abs(current.Value - previous.Value) <= 5;
    }
};

} // namespace State
} // namespace ESPressio

class ComparisonObserver final : public IRemoteStateManagerObserver {
public:
    int Accepted = 0;
    int Changed = 0;

    void OnRemoteStateAccepted(
        const DeviceIdentifier&,
        StateTypeId,
        StateEpoch,
        StateRevision,
        bool changed
    ) override {
        ++Accepted;
        if (changed) ++Changed;
    }
};

int main() {
    const AnalogValue baseline{100};
    const AnalogValue noise{104};
    const AnalogValue meaningful{106};

    // Default State comparison remains exact.
    assert(!StateValuesEqual<ExactAnalogState>(baseline, noise));
    assert(StateValueChanged<ExactAnalogState>(baseline, noise));

    // A State-definition-specific policy can suppress noise/deadband locally.
    assert(StateValuesEqual<DeadbandAnalogState>(baseline, noise));
    assert(!StateValueChanged<DeadbandAnalogState>(baseline, noise));
    assert(!StateValuesEqual<DeadbandAnalogState>(baseline, meaningful));
    assert(StateValueChanged<DeadbandAnalogState>(baseline, meaningful));

    // The comparison is keyed by State definition, even with the same Value type.
    static_assert(StateTypeIdOf<ExactAnalogState> != StateTypeIdOf<DeadbandAnalogState>);

    // Remote revisions are still accepted and advance even when the new value is
    // semantically equal, but observers see changed=false until the deadband is crossed.
    using Contract = StateContract<DeadbandAnalogState>;
    RemoteStateManager<Contract, 1> manager;
    ComparisonObserver observer;
    auto handle = manager.RegisterObserver(static_cast<IRemoteStateManagerObserver*>(&observer));
    const uint8_t mac[6] = {0x64, 0xB7, 0x08, 0x85, 0x63, 0x3D};
    const auto device = DeviceIdentifier::FromMacAddress(mac);

    assert(manager.Apply<DeadbandAnalogState>(device, 1, 1, baseline));
    assert(manager.Apply<DeadbandAnalogState>(device, 1, 2, noise));
    assert(manager.Apply<DeadbandAnalogState>(device, 1, 3, meaningful));
    assert(observer.Accepted == 3);
    assert(observer.Changed == 2);

    RemoteStateSnapshot<AnalogValue> snapshot;
    assert(manager.Read<DeadbandAnalogState>(device, snapshot));
    assert(snapshot.Revision == 3);
    assert(snapshot.Value.Value == meaningful.Value);

    return 0;
}
