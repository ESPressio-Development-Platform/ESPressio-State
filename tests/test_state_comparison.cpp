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

    return 0;
}
