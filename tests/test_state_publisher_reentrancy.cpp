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

class ReentrantPublicationObserver final : public IStatePublishedObserver<CounterState> {
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

class ReentrantLifecycleObserver final : public IStatePublisherObserver {
public:
    StatePublisher<Contract>* Publisher = nullptr;
    uint32_t* Replacement = nullptr;
    std::array<int, 8> Sequence{};
    std::size_t Count = 0;
    int Depth = 0;
    int MaximumDepth = 0;

    void Record(int value) {
        assert(Count < Sequence.size());
        Sequence[Count++] = value;
    }

    void OnStateSourceBound(const StateAddress&, StateEpoch, StateRevision revision) override {
        ++Depth;
        if (Depth > MaximumDepth) MaximumDepth = Depth;
        Record(10 + static_cast<int>(revision));
        if (revision == 1) {
            assert(Publisher->Unbind<CounterState>(StateUnbindMode::Retain));
            assert(Depth == 1);
        }
        --Depth;
    }

    void OnStateAvailabilityChanged(const StateAddress&, StateAvailabilityStatus previous, StateAvailabilityStatus current) override {
        ++Depth;
        if (Depth > MaximumDepth) MaximumDepth = Depth;
        if (current.Availability == StateAvailability::Available) Record(20);
        else if (current.Availability == StateAvailability::Unavailable) Record(30);
        (void)previous;
        --Depth;
    }

    void OnStateSourceUnbound(const StateAddress&, StateUnbindMode mode, StateEpoch, StateRevision revision) override {
        ++Depth;
        if (Depth > MaximumDepth) MaximumDepth = Depth;
        assert(mode == StateUnbindMode::Retain);
        Record(40 + static_cast<int>(revision));
        assert(Publisher->Bind<CounterState>(*Replacement));
        assert(Depth == 1);
        --Depth;
    }
};

static void TestPublicationReentrancy() {
    StatePublisher<Contract> publisher(Device());
    uint32_t authoritative = 10;
    assert(publisher.Bind<CounterState>(authoritative));

    ReentrantPublicationObserver observer;
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
}

static void TestLifecycleReentrancy() {
    StatePublisher<Contract> publisher(Device());
    uint32_t first = 10;
    uint32_t replacement = 20;

    ReentrantLifecycleObserver observer;
    observer.Publisher = &publisher;
    observer.Replacement = &replacement;
    auto handle = publisher.RegisterObserver(&observer);
    assert(handle);

    assert(publisher.Bind<CounterState>(first));

    assert(observer.MaximumDepth == 1);
    assert(observer.Count == 6);
    assert(observer.Sequence[0] == 11); // initial Bound, revision 1
    assert(observer.Sequence[1] == 20); // initial Available
    assert(observer.Sequence[2] == 30); // deferred Unbind -> Unavailable
    assert(observer.Sequence[3] == 41); // deferred Unbound, revision 1
    assert(observer.Sequence[4] == 12); // deferred re-Bind, revision 2
    assert(observer.Sequence[5] == 20); // re-Bind -> Available

    const auto registration = publisher.Registration<CounterState>();
    assert(registration.Bound);
    assert(registration.Revision == 2);

    LocalStateView<CounterState> view;
    assert(publisher.Read<CounterState>(view));
    assert(&view.ValueRef() == &replacement);
}

int main() {
    TestPublicationReentrancy();
    TestLifecycleReentrancy();
    return 0;
}
