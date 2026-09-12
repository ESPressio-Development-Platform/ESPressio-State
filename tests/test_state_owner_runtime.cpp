#include <ESPressio_States.hpp>
#include <ESPressio_RuntimeIdentity.hpp>
#include <ESPressio_TypeDirectory.hpp>
#include <cassert>
#include <string_view>
using namespace ESPressio;
namespace S=ESPressio::State;
struct Temperature2 final : S::State<Temperature2,int> {
    static constexpr S::StateTypeId TypeId{1012};
    static constexpr std::string_view CanonicalName="Test.State.Temperature2";
};
static Timing::QualifiedTime Captured(){ return {777,Timing::TimeReliability::Synchronized}; }
int main(){
    System::DeviceIdentifier::Storage bytes{};bytes[0]=5;
    assert(System::RuntimeIdentity::Install({System::DeviceIdentifier{bytes},System::RuntimeIncarnationId{10}})==System::RuntimeIdentity::InstallationStatus::Success);
    Primitive::TypeDirectory<1> directory;
    assert(directory.Register<Temperature2>()==Primitive::TypeDirectoryRegistrationStatus::Success);
    assert(directory.Initialize()==Primitive::TypeDirectoryInitializationStatus::Success);
    using R=S::Runtime<S::TypeConfiguration<Temperature2>>;
    R runtime;
    auto owner=runtime.BindOwner<Temperature2>();assert(owner);
    S::StateSnapshot<Temperature2> snapshot{};
    assert(!runtime.TryRead(snapshot));
    assert(runtime.Initialize(directory.View(),&Captured)==S::StateRuntimeStatus::Success);
    assert(owner.Set(1)==S::StateSetStatus::NotRunning);
    assert(runtime.Start()==S::StateRuntimeStatus::Success);
    assert(owner.Set(42)==S::StateSetStatus::Changed);
    assert(runtime.TryRead(snapshot));assert(snapshot.Value==42);
    assert(snapshot.TruthTime.Nanoseconds==777 && snapshot.TruthTime.Reliability==Timing::TimeReliability::Synchronized);
    assert((runtime.Version<Temperature2>()==S::StateVersion{false,1}));
    assert(owner.Set(42,{999,Timing::TimeReliability::Unqualified})==S::StateSetStatus::NoChange);
    assert(runtime.TryRead(snapshot));assert(snapshot.TruthTime.Nanoseconds==777);
    auto moved=std::move(owner);assert(!owner && moved);
    assert(moved.Set(43,{888,Timing::TimeReliability::Acquiring})==S::StateSetStatus::Changed);
    assert((runtime.Version<Temperature2>()==S::StateVersion{false,2}));
    moved=S::StateOwner<Temperature2>{};
    assert(runtime.TryRead(snapshot) && snapshot.Value==43);
    assert(!S::StateTypeRuntime<Temperature2>::Get().OwnerAlive());
    assert(!runtime.BindOwner<Temperature2>());
    assert(runtime.Shutdown()==S::StateRuntimeStatus::Success);
    assert(runtime.TryRead(snapshot) && snapshot.Value==43);
}
