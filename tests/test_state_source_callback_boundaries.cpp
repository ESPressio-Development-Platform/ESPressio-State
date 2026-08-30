#include <cassert>
#include <cstdint>

#include <ESPressio_State.hpp>

using namespace ESPressio::State;

struct SourceState {
    using Value = uint32_t;
    static constexpr StateTypeId Id = 0x530001;
};

using Contract = StateContract<SourceState>;

int main() {
    StatePublisher<Contract> publisher;

    bool replacedDuringPublish = false;
    assert(publisher.RegisterSource<SourceState>([&]() -> uint32_t {
        replacedDuringPublish = true;
        assert(publisher.RegisterSource<SourceState>([]() -> uint32_t {
            return 22U;
        }));
        return 11U;
    }));

    // The source callback must execute without the publisher mutex held. Its
    // synchronous source replacement must therefore complete, and the stale
    // value produced by the replaced source must not be committed.
    assert(!publisher.Publish<SourceState>());
    assert(replacedDuringPublish);

    assert(publisher.Publish<SourceState>());
    StateUpdate<uint32_t> snapshot;
    assert(publisher.Snapshot<SourceState>(snapshot));
    assert(snapshot.Value == 22U);

    bool removedDuringSnapshot = false;
    assert(publisher.RegisterSource<SourceState>([&]() -> uint32_t {
        removedDuringSnapshot = true;
        assert(publisher.UnregisterSource<SourceState>());
        return 33U;
    }));

    // Snapshot follows the same callback boundary and must reject a result
    // whose pinned registration was removed while the source was executing.
    assert(!publisher.Snapshot<SourceState>(snapshot));
    assert(removedDuringSnapshot);
    assert(!publisher.HasSource<SourceState>());

    return 0;
}
