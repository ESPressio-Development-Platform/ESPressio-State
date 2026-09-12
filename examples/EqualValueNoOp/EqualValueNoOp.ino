#include <ESPressio_States.hpp>
#include <ESPressio_TypeDirectory.hpp>
#include <cassert>
#include <string_view>

using namespace ESPressio;
namespace S=ESPressio::State;

struct CounterState final:S::State<CounterState,int> {
    static constexpr S::StateTypeId TypeId{0x1002};
    static constexpr std::string_view CanonicalName="Example.State.Counter";
};

Primitive::TypeDirectory<1> directory;
S::Runtime<S::TypeConfiguration<CounterState>> states;
S::StateOwner<CounterState> owner;

void setup() {
    (void)directory.Register<CounterState>();
    (void)directory.Initialize();
    owner=states.BindOwner<CounterState>();
    (void)states.Initialize(directory.View());
    (void)states.Start();

    assert(owner.Set(7,{100,Timing::TimeReliability::Synchronized})==S::StateSetStatus::Changed);
    const auto version=states.Version<CounterState>();
    assert(owner.Set(7,{999,Timing::TimeReliability::Unqualified})==S::StateSetStatus::NoChange);

    S::StateSnapshot<CounterState> snapshot{};
    assert(states.TryRead(snapshot));
    assert(snapshot.Value==7 && snapshot.TruthTime.Nanoseconds==100);
    assert(states.Version<CounterState>()==version);
}

void loop() {}
