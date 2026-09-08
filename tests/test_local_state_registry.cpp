#include <cassert>
#include <cstdint>

#include <ESPressio_State.hpp>

using namespace ESPressio::State;

/**
 * ESPressio Memory Audit
 * Members: none (standalone empty object occupies 1 byte; an eligible empty base may be optimized to 0 bytes).
 * Total Memory: 1 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
struct CounterState {
    using Value = uint32_t;
    static constexpr StateTypeId Id = 1;
};
/**
 * ESPressio Memory Audit
 * Members: none (standalone empty object occupies 1 byte; an eligible empty base may be optimized to 0 bytes).
 * Total Memory: 1 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
struct EnabledState {
    using Value = bool;
    static constexpr StateTypeId Id = 2;
};
using Contract = StateContract<CounterState, EnabledState>;

int main() {
    // Backward-compatible unconfigured runtimes still begin at epoch one.
    LocalStateRegistry<Contract> registry;
    uint32_t counter = 10;

    assert(registry.Bind<CounterState>(counter));
    assert(!registry.Bind<CounterState>(counter));

    auto registration = registry.Registration<CounterState>();
    assert(registration.Bound);
    assert(!registration.Retained);
    assert(registration.Epoch == 1);
    assert(registration.Revision == 1);

    LocalStateView<CounterState> view;
    assert(registry.Read<CounterState>(view));
    assert(&view.ValueRef() == &counter);
    assert(view.ValueRef() == 10);
    assert(view.Revision == 1);

    counter = 11;
    StateRevision revision = 0;
    assert(registry.NotifyChanged<CounterState>(revision));
    assert(revision == 2);
    assert(registry.Read<CounterState>(view));
    assert(view.ValueRef() == 11);
    assert(view.Revision == 2);

    assert(registry.Unbind<CounterState>(StateUnbindMode::Retain));
    assert(!registry.Read<CounterState>(view));
    registration = registry.Registration<CounterState>();
    assert(!registration.Bound && registration.Retained);
    assert(registration.Epoch == 1 && registration.Revision == 2);

    counter = 12;
    assert(registry.Bind<CounterState>(counter));
    registration = registry.Registration<CounterState>();
    assert(registration.Epoch == 1 && registration.Revision == 3);
    assert(registry.Read<CounterState>(view));
    assert(view.ValueRef() == 12 && view.Revision == 3);
    assert(registry.NotifyChanged<CounterState>(revision));
    assert(revision == 4);

    assert(registry.Unbind<CounterState>(StateUnbindMode::Discard));
    registration = registry.Registration<CounterState>();
    assert(!registration.Bound && !registration.Retained && registration.Revision == 0);

    assert(registry.Bind<CounterState>(counter));
    registration = registry.Registration<CounterState>();
    assert(registration.Epoch == 2);
    assert(registration.Revision == 1);

    bool enabled = false;
    {
        auto binding = registry.BindScoped<EnabledState>(enabled, StateUnbindMode::Retain);
        assert(binding);
        assert(registry.Registration<EnabledState>().Bound);
        assert(registry.Registration<EnabledState>().Revision == 1);
    }
    assert(!registry.Registration<EnabledState>().Bound);
    assert(registry.Registration<EnabledState>().Retained);

    // A boot/runtime may seed every subsequently constructed registry with one fresh non-zero epoch.
    constexpr StateEpoch bootEpoch = 0x4A17B20DU;
    assert(StateRuntimeEpoch::Configure(bootEpoch));
    assert(StateRuntimeEpoch::Configure(bootEpoch));
    assert(!StateRuntimeEpoch::Configure(static_cast<StateEpoch>(bootEpoch + 1U)));

    LocalStateRegistry<Contract> restartedRegistry;
    uint32_t restartedCounter = 1U;
    bool restartedEnabled = true;
    assert(restartedRegistry.Bind<CounterState>(restartedCounter));
    assert(restartedRegistry.Bind<EnabledState>(restartedEnabled));
    assert(restartedRegistry.Registration<CounterState>().Epoch == bootEpoch);
    assert(restartedRegistry.Registration<CounterState>().Revision == 1U);
    assert(restartedRegistry.Registration<EnabledState>().Epoch == bootEpoch);

    // Discard begins a new lineage relative to the captured runtime epoch.
    assert(restartedRegistry.Unbind<CounterState>(StateUnbindMode::Discard));
    assert(restartedRegistry.Bind<CounterState>(restartedCounter));
    assert(restartedRegistry.Registration<CounterState>().Epoch == bootEpoch + 1U);
    assert(restartedRegistry.Registration<CounterState>().Revision == 1U);

    return 0;
}
