#include <cassert>
#include <cstdint>

#include <ESPressio_State.hpp>

using namespace ESPressio::State;

struct CounterState {
    using Value = uint32_t;
    static constexpr StateTypeId Id = 1;
};
struct EnabledState {
    using Value = bool;
    static constexpr StateTypeId Id = 2;
};
using Contract = StateContract<CounterState, EnabledState>;

int main() {
    LocalStateRegistry<Contract> registry;
    uint32_t counter = 10;

    assert(registry.Bind<CounterState>(counter));
    assert(!registry.Bind<CounterState>(counter));

    auto registration = registry.Registration<CounterState>();
    assert(registration.Bound);
    assert(!registration.Retained);
    assert(registration.Epoch == 1);
    assert(registration.Revision == 0);

    LocalStateView<CounterState> view;
    assert(registry.Read<CounterState>(view));
    assert(&view.ValueRef() == &counter);
    assert(view.ValueRef() == 10);

    counter = 11;
    StateRevision revision = 0;
    assert(registry.NotifyChanged<CounterState>(revision));
    assert(revision == 1);
    assert(registry.Read<CounterState>(view));
    assert(view.ValueRef() == 11);
    assert(view.Revision == 1);

    assert(registry.Unbind<CounterState>(StateUnbindMode::Retain));
    assert(!registry.Read<CounterState>(view));
    registration = registry.Registration<CounterState>();
    assert(!registration.Bound && registration.Retained);
    assert(registration.Epoch == 1 && registration.Revision == 1);

    assert(registry.Bind<CounterState>(counter));
    registration = registry.Registration<CounterState>();
    assert(registration.Epoch == 1 && registration.Revision == 1);
    assert(registry.NotifyChanged<CounterState>(revision));
    assert(revision == 2);

    assert(registry.Unbind<CounterState>(StateUnbindMode::Discard));
    registration = registry.Registration<CounterState>();
    assert(!registration.Bound && !registration.Retained && registration.Revision == 0);

    assert(registry.Bind<CounterState>(counter));
    registration = registry.Registration<CounterState>();
    assert(registration.Epoch == 2);
    assert(registration.Revision == 0);

    bool enabled = false;
    {
        auto binding = registry.BindScoped<EnabledState>(enabled, StateUnbindMode::Retain);
        assert(binding);
        assert(registry.Registration<EnabledState>().Bound);
    }
    assert(!registry.Registration<EnabledState>().Bound);
    assert(registry.Registration<EnabledState>().Retained);

    return 0;
}
