#include <ESPressio_States.hpp>
#include <ESPressio_TypeDirectory.hpp>
#include <cassert>
#include <string_view>

using namespace ESPressio;
namespace S=ESPressio::State;

struct MeasurementState final:S::State<MeasurementState,int> {
    static constexpr S::StateTypeId TypeId{0x1003};
    static constexpr std::string_view CanonicalName="Example.State.Measurement";
};

Primitive::TypeDirectory<1> directory;
S::Runtime<S::TypeConfiguration<MeasurementState>> states;
S::StateOwner<MeasurementState> owner;

void setup() {
    (void)directory.Register<MeasurementState>();
    (void)directory.Initialize();
    owner=states.BindOwner<MeasurementState>();
    (void)states.Initialize(directory.View());
    (void)states.Start();

    const Timing::QualifiedTime truth{123456789,Timing::TimeReliability::Holdover};
    assert(owner.Set(42,truth)==S::StateSetStatus::Changed);

    S::StateSnapshot<MeasurementState> snapshot{};
    assert(states.TryRead(snapshot));
    assert(snapshot.TruthTime.Nanoseconds==truth.Nanoseconds);
    assert(snapshot.TruthTime.Reliability==truth.Reliability);
}

void loop() {}
