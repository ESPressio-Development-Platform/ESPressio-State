#include <cassert>
#include <cstdint>
#include <ESPressio_State.hpp>
using namespace ESPressio::State;

/**
 * ESPressio Memory Audit
 * Members:
 * - Reading (int): 4 bytes [0 bytes dynamic allocation]
 * Total Memory: 4 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
struct Value { int Reading = 0; bool operator==(const Value& other) const { return Reading == other.Reading; } };
/**
 * ESPressio Memory Audit
 * Members: none (standalone empty object occupies 1 byte; an eligible empty base may be optimized to 0 bytes).
 * Total Memory: 1 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
struct DeadbandState { using Value = ::Value; static constexpr StateTypeId Id = 1; };

/**
 * ESPressio Memory Audit
 * Inherited Memory Total: 4 bytes [0 bytes dynamic allocation]
 * Members:
 * - Accepted (int): 4 bytes [0 bytes dynamic allocation]
 * Total Memory: 8 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
class Observer final : public IRemoteStateManagerObserver {
public:
    int Accepted = 0;
    void OnRemoteStateAccepted(const DeviceIdentifier&, StateTypeId, StateEpoch, StateRevision, bool) override { ++Accepted; }
};

int main() {
    using Contract = StateContract<DeadbandState>;
    DeviceIdentifier::Storage identity{}; identity[15] = 1; const DeviceIdentifier device(identity);
    RemoteStateManager<Contract, 1> manager; Observer observer;
    auto handle = manager.RegisterObserver(static_cast<IRemoteStateManagerObserver*>(&observer));
    assert(manager.Apply<DeadbandState>(device, 1, 1, {100}));
    assert(manager.Apply<DeadbandState>(device, 1, 2, {101}));
    assert(observer.Accepted == 2);
    return 0;
}
