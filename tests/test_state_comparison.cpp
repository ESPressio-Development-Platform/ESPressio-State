#include <cassert>
#include <cmath>
#include <cstdint>

#include <ESPressio_State.hpp>

using namespace ESPressio::State;

/**
 * ESPressio Memory Audit
 * Members:
 * - Value (float): 4 bytes [0 bytes dynamic allocation]
 * Total Memory: 4 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
struct AnalogValue {
    float Value = 0.0f;
    bool operator==(const AnalogValue& other) const { return Value == other.Value; }
};

/**
 * ESPressio Memory Audit
 * Members: none (standalone empty object occupies 1 byte; an eligible empty base may be optimized to 0 bytes).
 * Total Memory: 1 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
struct ExactAnalogState {
    using Value = AnalogValue;
    static constexpr StateTypeId Id = 1;
};

/**
 * ESPressio Memory Audit
 * Members: none (standalone empty object occupies 1 byte; an eligible empty base may be optimized to 0 bytes).
 * Total Memory: 1 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
struct DeadbandAnalogState {
    using Value = AnalogValue;
    static constexpr StateTypeId Id = 2;
};

/**
 * ESPressio Memory Audit
 * Members: none (standalone empty object occupies 1 byte; an eligible empty base may be optimized to 0 bytes).
 * Total Memory: 1 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
template<>
struct ESPressio::State::StateComparison<ExactAnalogState> {
    static bool Equals(const AnalogValue& left, const AnalogValue& right) {
        return left.Value == right.Value;
    }
};

/**
 * ESPressio Memory Audit
 * Members: none (standalone empty object occupies 1 byte; an eligible empty base may be optimized to 0 bytes).
 * Total Memory: 1 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
template<>
struct ESPressio::State::StateComparison<DeadbandAnalogState> {
    static bool Equals(const AnalogValue& left, const AnalogValue& right) {
        return std::fabs(left.Value - right.Value) < 0.5f;
    }
};

using Contract = StateContract<ExactAnalogState, DeadbandAnalogState>;

/**
 * ESPressio Memory Audit
 * Inherited Memory Total: 4 bytes [0 bytes dynamic allocation]
 * Members:
 * - Published (int): 4 bytes [0 bytes dynamic allocation]
 * Total Memory: 8 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
class PublisherObserver final : public IStatePublisherObserver {
public:
    int Published = 0;
    void OnStatePublished(const StateAddress&, StateEpoch, StateRevision) override {
        ++Published;
    }
};

int main() {
    DeviceIdentifier::Storage identity{};
    identity[15] = 1;
    const DeviceIdentifier device(identity);

    // StateComparison remains an application/domain decision helper. It does
    // not create a shadow copy inside reference-backed State publication.
    const AnalogValue baseline{100.0f};
    assert(!StateValueChanged<DeadbandAnalogState>(baseline, {100.2f}));
    assert(StateValueChanged<DeadbandAnalogState>(baseline, {101.0f}));
    assert(StateValueChanged<ExactAnalogState>(baseline, {100.2f}));

    StatePublisher<Contract> publisher(device);
    PublisherObserver observer;
    auto handle = publisher.RegisterObserver(&observer);
    assert(handle);

    AnalogValue authoritative{100.0f};
    assert(publisher.Bind<DeadbandAnalogState>(authoritative));

    // The application can suppress semantically equivalent mutation before
    // calling NotifyChanged.
    const AnalogValue candidate{100.2f};
    if (StateValueChanged<DeadbandAnalogState>(authoritative, candidate)) {
        authoritative = candidate;
        assert(publisher.NotifyChanged<DeadbandAnalogState>());
    }
    assert(observer.Published == 0);

    // Explicit NotifyChanged is authoritative and must not re-run equality.
    authoritative = {101.0f};
    assert(publisher.NotifyChanged<DeadbandAnalogState>());
    assert(observer.Published == 1);

    authoritative = {101.1f};
    assert(publisher.NotifyChanged<DeadbandAnalogState>());
    assert(observer.Published == 2);

    return 0;
}
