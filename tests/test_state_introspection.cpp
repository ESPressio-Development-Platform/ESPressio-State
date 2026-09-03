#include <cassert>
#include <cstdint>
#include <cstring>
#include <type_traits>

#include <ESPressio_StateIntrospection.hpp>
#include <ESPressio_StateSerialization.hpp>

using namespace ESPressio::State;

struct LedState { using Value = bool; static constexpr StateTypeId Id = 1; static constexpr const char* Name = "led.enabled"; };
struct CounterState { using Value = int32_t; static constexpr StateTypeId Id = 2; static constexpr const char* Name = "counter.value"; };
struct UnnamedState { using Value = uint16_t; static constexpr StateTypeId Id = 3; };
using Contract = StateContract<LedState, CounterState, UnnamedState>;

static DeviceIdentifier Device(uint8_t marker) {
    DeviceIdentifier::Storage bytes{}; bytes[0] = marker; return DeviceIdentifier(bytes);
}

int main() {
    RemoteStateManager<Contract, 3> manager;
    const auto first = Device(1); const auto second = Device(2);

    assert(manager.SetReachability(first, StateSourceReachability::Reachable));
    assert(manager.SetReachability(second, StateSourceReachability::Stale));
    assert(manager.Apply<LedState>(first, 1, 1, true));
    assert(manager.Apply<CounterState>(first, 1, 1, 42));
    assert(manager.Apply<UnnamedState>(second, 2, 7, 99));

    const char* name = nullptr;
    assert(StateIntrospection<Contract>::TryGetName(1, name));
    assert(name != nullptr && std::strcmp(name, "led.enabled") == 0);
    assert(!StateIntrospection<Contract>::TryGetName(3, name)); assert(name == nullptr);

    StateTypeId typeId = 0;
    assert(StateIntrospection<Contract>::TryGetTypeId("counter.value", typeId)); assert(typeId == CounterState::Id);
    assert(!StateIntrospection<Contract>::TryGetTypeId("missing", typeId)); assert(typeId == 0);

    std::size_t deviceCount = 0; manager.ForEachDevice([&](const RemoteDeviceSnapshot&) { ++deviceCount; }); assert(deviceCount == 2);

    RemoteStateIntrospectionSnapshot<CounterState> counter;
    assert(StateIntrospection<Contract>::Read<CounterState>(manager, first, counter));
    assert(counter.Name != nullptr && counter.State.HasValue && counter.State.Value == 42);
    assert(counter.State.Availability.Availability == StateAvailability::Available);
    assert(counter.State.Reachability == StateSourceReachability::Reachable);

    std::size_t firstStateCount = 0;
    StateIntrospection<Contract>::ForEachState(manager, first, [&](const auto&) { ++firstStateCount; });
    assert(firstStateCount == 2);

    std::size_t remoteStateCount = 0;
    StateIntrospection<Contract>::ForEachRemoteState(manager, [&](const auto&) { ++remoteStateCount; });
    assert(remoteStateCount == 3);

    SerializedRemoteState<CounterState> serializedCounter;
    assert(StateSerialization<Contract>::Serialize<CounterState>(manager, first, serializedCounter));
    assert(serializedCounter.TypeId == CounterState::Id && serializedCounter.Revision == 1);
    assert(serializedCounter.Availability.Availability == StateAvailability::Available);
    assert(serializedCounter.Reachability == StateSourceReachability::Reachable);
    assert(serializedCounter.PayloadSize == sizeof(int32_t));

    int32_t decodedCounter = 0;
    assert(StateCodec<CounterState>::Decode(serializedCounter.Payload.data(), serializedCounter.PayloadSize, decodedCounter));
    assert(decodedCounter == 42);

    bool runtimeSelected = false;
    assert(StateSerialization<Contract>::Serialize(manager, second, UnnamedState::Id, [&](const auto& record) {
        using Record = std::decay_t<decltype(record)>;
        if constexpr (std::is_same_v<Record, SerializedRemoteState<UnnamedState>>) {
            uint16_t value = 0;
            assert(record.Name == nullptr);
            assert(record.Availability.Availability == StateAvailability::Stale);
            assert(record.Reachability == StateSourceReachability::Stale);
            assert(StateCodec<UnnamedState>::Decode(record.Payload.data(), record.PayloadSize, value));
            assert(value == 99); runtimeSelected = true;
        }
    }));
    assert(runtimeSelected);

    std::size_t serializedCount = 0;
    StateSerialization<Contract>::ForEachRemoteState(manager, [&](const auto& record) {
        assert(record.PayloadSize <= record.Payload.size()); ++serializedCount;
    });
    assert(serializedCount == 3);
    return 0;
}
