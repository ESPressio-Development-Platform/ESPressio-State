#include <ESPressio_States.hpp>
#include <ESPressio_RuntimeIdentity.hpp>
#include <ESPressio_TypeDirectory.hpp>
#include <cassert>
#include <string_view>
using namespace ESPressio;
namespace S=ESPressio::State;
struct Temperature final : S::State<Temperature,int> {
    static constexpr S::StateTypeId TypeId{1002};
    static constexpr std::string_view CanonicalName="Test.State.Temperature";
};
struct Deadband final : S::State<Deadband,int> {
    static constexpr S::StateTypeId TypeId{1003};
    static constexpr std::string_view CanonicalName="Test.State.Deadband";
};
namespace ESPressio::State {
template<> struct StateComparison<Deadband> {
    static bool Equals(const int& a,const int& b) noexcept { return (a>b?a-b:b-a)<3; }
};
}
static Timing::QualifiedTime Captured(){ return {1234,Timing::TimeReliability::Holdover}; }
int main(){
    System::DeviceIdentifier::Storage bytes{};bytes[0]=4;
    assert(System::RuntimeIdentity::Install({System::DeviceIdentifier{bytes},System::RuntimeIncarnationId{9}})==System::RuntimeIdentity::InstallationStatus::Success);
    Primitive::TypeDirectory<2> directory;
    assert(directory.Register<Temperature>()==Primitive::TypeDirectoryRegistrationStatus::Success);
    assert(directory.Register<Deadband>()==Primitive::TypeDirectoryRegistrationStatus::Success);
    assert(directory.Initialize()==Primitive::TypeDirectoryInitializationStatus::Success);
    using R=S::Runtime<S::TypeConfiguration<Temperature>,S::TypeConfiguration<Deadband>>;
    R runtime;
    auto temperature=runtime.BindOwner<Temperature>();
    auto duplicate=runtime.BindOwner<Temperature>();
    assert(temperature && !duplicate);
    auto deadband=runtime.BindOwner<Deadband>();assert(deadband);
    assert(!S::StateTypeRuntime<Temperature>::Get().HasValue());
    assert(runtime.Initialize(directory.View(),&Captured)==S::StateRuntimeStatus::InvalidConfiguration);
}
