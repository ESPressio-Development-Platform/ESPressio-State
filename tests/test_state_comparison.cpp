#include <cassert>
#include <cstdint>
#include <cmath>

#include <ESPressio_State.hpp>

using namespace ESPressio::State;

struct AnalogValue { float Value = 0.0f; };
struct ExactAnalogState { using Value = AnalogValue; static constexpr StateTypeId Id = 1; };
struct DeadbandAnalogState { using Value = AnalogValue; static constexpr StateTypeId Id = 2; };

template<>
struct ESPressio::State::StateComparison<ExactAnalogState> {
    static bool Equivalent(const AnalogValue& left, const AnalogValue& right) { return left.Value == right.Value; }
};
template<>
struct ESPressio::State::StateComparison<DeadbandAnalogState> {
    static bool Equivalent(const AnalogValue& left, const AnalogValue& right) { return std::fabs(left.Value - right.Value) < 0.5f; }
};

using Contract = StateContract<ExactAnalogState, DeadbandAnalogState>;

class PublisherObserver final : public IStatePublisherObserver {
public:
    int Published = 0;
    void OnStatePublished(StateTypeId, StateEpoch, StateRevision) override { ++Published; }
};
class ComparisonObserver final : public IRemoteStateManagerObserver {
public:
    int Accepted = 0, Changed = 0;
    void OnRemoteStateAccepted(const DeviceIdentifier&, StateTypeId, StateEpoch, StateRevision, bool changed) override { ++Accepted; if (changed) ++Changed; }
};

int main() {
    DeviceIdentifier::Storage identity{}; identity[15] = 1; const DeviceIdentifier device(identity);
    StatePublisher<Contract> publisher(device);
    PublisherObserver publisherObserver; auto publisherHandle = publisher.RegisterObserver(&publisherObserver);
    AnalogValue authoritative{100.0f};
    assert(publisher.RegisterSource<DeadbandAnalogState>([&] { return authoritative; }));
    assert(publisher.Publish<DeadbandAnalogState>());
    authoritative = {100.2f}; assert(!publisher.Publish<DeadbandAnalogState>());
    authoritative = {101.0f}; assert(publisher.Publish<DeadbandAnalogState>());
    authoritative = {102.0f}; assert(publisher.Publish<DeadbandAnalogState>());
    assert(publisherObserver.Published == 3);

    RemoteStateManager<Contract, 1> manager; ComparisonObserver observer;
    auto handle = manager.RegisterObserver(static_cast<IRemoteStateManagerObserver*>(&observer));
    assert(manager.Apply<DeadbandAnalogState>(device, 1, 1, {100.0f}));
    assert(manager.Apply<DeadbandAnalogState>(device, 1, 2, {100.2f}));
    assert(manager.Apply<DeadbandAnalogState>(device, 1, 3, {101.0f}));
    assert(observer.Accepted == 3 && observer.Changed == 3);
    return 0;
}
