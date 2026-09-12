#include <ESPressio_States.hpp>
#include <ESPressio_TypeDirectory.hpp>
#include <cassert>
#include <string_view>

using namespace ESPressio;
namespace S=ESPressio::State;

struct PositionState final:S::State<PositionState,int> {
    static constexpr S::StateTypeId TypeId{0x1004};
    static constexpr std::string_view CanonicalName="Example.State.Position";
};

Primitive::TypeDirectory<1> directory;
S::Runtime<S::TypeConfiguration<PositionState>> states;
S::StateOwner<PositionState> owner;

void setup() {
    (void)directory.Register<PositionState>();
    (void)directory.Initialize();
    owner=states.BindOwner<PositionState>();
    (void)states.Initialize(directory.View());
    (void)states.Start();

    assert(owner.Set(10,{10,Timing::TimeReliability::Synchronized})==S::StateSetStatus::Changed);
    S::StateSnapshot<PositionState> earlier{};
    assert(states.TryRead(earlier));

    assert(owner.Set(20,{20,Timing::TimeReliability::Synchronized})==S::StateSetStatus::Changed);
    S::StateSnapshot<PositionState> current{};
    assert(states.TryRead(current));

    // Snapshots are owned immutable copies; a later Set cannot mutate an earlier read.
    assert(earlier.Value==10 && earlier.TruthTime.Nanoseconds==10);
    assert(current.Value==20 && current.TruthTime.Nanoseconds==20);
}

void loop() {}
