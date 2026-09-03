#include <cassert>
#include <cstdint>
#include <ESPressio_State.hpp>
using namespace ESPressio::State;

struct Value { int Reading = 0; bool operator==(const Value& other) const { return Reading == other.Reading; } };
struct DeadbandState { using Value = ::Value; static constexpr StateTypeId Id = 1; };

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
