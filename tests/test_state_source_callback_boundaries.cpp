#include <cassert>
#include <cstdint>

#include <ESPressio_State.hpp>

using namespace ESPressio::State;

struct SourceState {
    using Value = uint32_t;
    static constexpr StateTypeId Id = 0x530001;
};

using Contract = StateContract<SourceState>;

class ReentrantObserver final : public IStatePublisherObserver {
public:
    StatePublisher<Contract>* Publisher = nullptr;
    int Bound = 0;
    int Published = 0;
    bool SnapshotSucceededDuringBound = false;
    bool UnboundDuringPublication = false;

    void OnStateSourceBound(const StateAddress&, StateEpoch, StateRevision) override {
        ++Bound;
        StateUpdate<uint32_t> snapshot;
        SnapshotSucceededDuringBound = Publisher->Snapshot<SourceState>(snapshot);
        assert(snapshot.Value == 11U);
    }

    void OnStatePublished(const StateAddress&, StateEpoch, StateRevision) override {
        ++Published;
        UnboundDuringPublication = Publisher->Unbind<SourceState>(StateUnbindMode::Retain);
    }
};

static DeviceIdentifier Device() {
    DeviceIdentifier::Storage bytes{};
    bytes[15] = 1;
    return DeviceIdentifier(bytes);
}

int main() {
    StatePublisher<Contract> publisher(Device());
    ReentrantObserver observer;
    observer.Publisher = &publisher;
    auto handle = publisher.RegisterObserver(&observer);
    assert(handle);

    uint32_t authoritative = 11U;
    assert(publisher.Bind<SourceState>(authoritative));

    // Lifecycle callbacks execute after registry mutation has committed and
    // without a LocalStateRegistry lock being held, so read-only re-entry is safe.
    assert(observer.Bound == 1);
    assert(observer.SnapshotSucceededDuringBound);

    authoritative = 22U;
    StateRevision revision = 0;
    assert(publisher.NotifyChanged<SourceState>(&revision));
    assert(revision == 2);

    // Publication callbacks likewise execute outside registry mutation. A
    // synchronous lifecycle transition can therefore complete without a lock cycle.
    assert(observer.Published == 1);
    assert(observer.UnboundDuringPublication);
    assert(!publisher.Registration<SourceState>().Bound);
    assert(publisher.Registration<SourceState>().Retained);

    return 0;
}
