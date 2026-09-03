#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>

#include <ESPressio_State.hpp>

using namespace ESPressio::State;

struct CounterState {
    using Value = uint32_t;
    static constexpr StateTypeId Id = 0x7001;
};

using Contract = StateContract<CounterState>;

static DeviceIdentifier Device() {
    DeviceIdentifier::Storage bytes{};
    bytes[15] = 1;
    return DeviceIdentifier(bytes);
}

class ReentrantObserver final : public IStatePublishedObserver<CounterState> {
public:
    StatePublisher<Contract>* Publisher = nullptr;
    uint32_t* Source = nullptr;
    std::array<StateRevision, 4> Revisions{};
    std::array<uint32_t, 4> Values{};
    std::size_t Count = 0;
    int Depth = 0;
    int MaximumDepth = 0;

    void OnStatePublished(StateTag<CounterState>, const StateUpdate<uint32_t>& update) override {
        ++Depth;
        if (Depth > MaximumDepth) MaximumDepth = Depth;
        assert(Count < Revisions.size());
        Revisions[Count] = update.Header.Revision;
        Values[Count] = update.Value;
        ++Count;

        if (update.Header.Revision == 2) {
            // Both mutations occur while revision 2 is notifying. State is
            // latest-fact data, so revision 3 may be coalesced by revision 4,
            // but neither call may recursively enter this observer.
            *Source = 20;
            StateRevision firstDeferred = 0;
            assert(Publisher->NotifyChanged<CounterState>(&firstDeferred));
            assert(firstDeferred == 3);
            assert(Depth == 1);

            *Source = 30;
            StateRevision secondDeferred = 0;
            assert(Publisher->NotifyChanged<CounterState>(&secondDeferred));
            assert(secondDeferred == 4);
            assert(Depth == 1);
        }

        --Depth;
    }
};

int main() {
    StatePublisher<Contract> publisher(Device());
    uint32_t authoritative = 10;
    assert(publisher.Bind<CounterState>(authoritative));

    ReentrantObserver observer;
    observer.Publisher = &publisher;
    observer.Source = &authoritative;
    auto handle = publisher.RegisterPublishedObserver<CounterState>(&observer);
    assert(handle);

    StateRevision committed = 0;
    assert(publisher.NotifyChanged<CounterState>(&committed));
    assert(committed == 2);

    assert(observer.MaximumDepth == 1);
    assert(observer.Count == 2);
    assert(observer.Revisions[0] == 2);
    assert(observer.Values[0] == 10);
    assert(observer.Revisions[1] == 4);
    assert(observer.Values[1] == 30);

    const auto registration = publisher.Registration<CounterState>();
    assert(registration.Revision == 4);
    return 0;
}
