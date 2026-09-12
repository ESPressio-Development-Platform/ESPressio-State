#include <ESPressio_States.hpp>
#include <ESPressio_RuntimeIdentity.hpp>
#include <ESPressio_TypeDirectory.hpp>
#include <cassert>

using namespace ESPressio;
namespace S=ESPressio::State;

static Timing::QualifiedTime Capture(){return {4711,Timing::TimeReliability::Synchronized};}

int main(){
    System::DeviceIdentifier::Storage bytes{};
    bytes[0]=0x44;
    assert(System::RuntimeIdentity::Install({System::DeviceIdentifier{bytes},System::RuntimeIncarnationId{37}})
           ==System::RuntimeIdentity::InstallationStatus::Success);

    Primitive::TypeDirectory<1> directory;
    assert(directory.Register<S::DeviceRuntimeIncarnationState>()
           ==Primitive::TypeDirectoryRegistrationStatus::Success);
    assert(directory.Initialize()==Primitive::TypeDirectoryInitializationStatus::Success);

    using Runtime=S::Runtime<S::TypeConfiguration<S::DeviceRuntimeIncarnationState>>;
    Runtime runtime;
    assert(runtime.Initialize(directory.View(),&Capture)==S::StateRuntimeStatus::Success);

    const auto* common=directory.View().Find(
        {S::StateFamilyId,S::DeviceRuntimeIncarnationState::TypeId.Value()});
    assert(common);
    const auto* descriptor=S::GetStateTypeDescriptor(*common);
    assert(descriptor && !descriptor->HasOwner());

    assert(runtime.Start()==S::StateRuntimeStatus::Success);
    S::StateSnapshot<S::DeviceRuntimeIncarnationState> snapshot{};
    assert(runtime.TryRead(snapshot));
    assert(snapshot.Value.Incarnation==37);
    assert(snapshot.TruthTime.Nanoseconds==4711);
    assert(snapshot.TruthTime.Reliability==Timing::TimeReliability::Synchronized);
    assert((runtime.Version<S::DeviceRuntimeIncarnationState>()==S::StateVersion{false,1}));
    assert(runtime.Shutdown()==S::StateRuntimeStatus::Success);
}
