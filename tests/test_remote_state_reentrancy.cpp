#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>

#include <ESPressio_State.hpp>

using namespace ESPressio::State;

struct CounterState {
    using Value = uint32_t;
    static constexpr StateTypeId Id = 0x7101;
};

using Contract = StateContract<CounterState>;

static DeviceIdentifier Device() {
    DeviceIdentifier::Storage bytes{};
    bytes[15] = 0x31;
    return DeviceIdentifier(bytes);
}

class ReentrantRemoteObserver final : public IRemoteStateManagerObserver {
public:
    RemoteStateManager<Contract, 2>* Manager = nullptr;
    DeviceIdentifier Identifier{};
    std::array<int, 12> Sequence{};
    std::size_t Count = 0;
    int Depth = 0;
    int MaximumDepth = 0;

    void Record(int value) {
        assert(Count < Sequence.size());
        Sequence[Count++] = value;
    }

    void Enter() {
        ++Depth;
        if (Depth > MaximumDepth) MaximumDepth = Depth;
    }

    void Leave() { --Depth; }

    void OnRemoteStateDeviceRegistered(const DeviceIdentifier&) override {
        Enter();
        Record(1);
        Leave();
    }

    void OnRemoteStateAccepted(const DeviceIdentifier&, StateTypeId, StateEpoch, StateRevision revision, bool) override {
        Enter();
        Record(10 + static_cast<int>(revision));
        if (revision == 1) {
            assert(Manager->Apply<CounterState>(Identifier, 1, 2, 20));
            assert(Manager->ApplyAvailability<CounterState>(Identifier, StateAvailability::Unavailable, StateAvailabilityReason::SourceUnbound));
            assert(Manager->SetReachability(Identifier, StateSourceReachability::Reachable));
            assert(Depth == 1);
        }
        Leave();
    }

    void OnRemoteStateReachabilityChanged(const DeviceIdentifier&, StateSourceReachability, StateSourceReachability current) override {
        Enter();
        assert(current == StateSourceReachability::Reachable);
        Record(40);
        Leave();
    }

    void OnRemoteStateAvailabilityChanged(const StateAddress&, StateAvailabilityStatus, StateAvailabilityStatus current) override {
        Enter();
        if (current.Availability == StateAvailability::Stale) Record(20);
        else if (current.Availability == StateAvailability::Unavailable) Record(30);
        else if (current.Availability == StateAvailability::Available) Record(50);
        Leave();
    }
};

int main() {
    RemoteStateManager<Contract, 2> manager;
    ReentrantRemoteObserver observer;
    observer.Manager = &manager;
    observer.Identifier = Device();
    auto handle = manager.RegisterObserver(&observer);
    assert(handle);

    assert(manager.Apply<CounterState>(observer.Identifier, 1, 1, 10));

    assert(observer.MaximumDepth == 1);
    assert(observer.Count == 6);
    assert(observer.Sequence[0] == 1);
    assert(observer.Sequence[1] == 11);
    assert(observer.Sequence[2] == 20);
    assert(observer.Sequence[3] == 12);
    assert(observer.Sequence[4] == 30);
    assert(observer.Sequence[5] == 40);

    RemoteStateSnapshot<uint32_t> snapshot;
    assert(manager.Read<CounterState>(observer.Identifier, snapshot));
    assert(snapshot.HasValue);
    assert(snapshot.Value == 20);
    assert(snapshot.Revision == 2);
    assert(snapshot.Reachability == StateSourceReachability::Reachable);
    assert(snapshot.Availability.Availability == StateAvailability::Unavailable);
    assert(snapshot.Availability.Reason == StateAvailabilityReason::SourceUnbound);
    return 0;
}
