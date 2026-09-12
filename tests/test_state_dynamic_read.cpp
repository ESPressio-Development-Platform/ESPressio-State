#include <ESPressio_States.hpp>
#include <ESPressio_RuntimeIdentity.hpp>
#include <ESPressio_SerializationMacros.hpp>
#include <ESPressio_TypeDirectory.hpp>
#include <array>
#include <cassert>
#include <cstdint>
#include <string_view>

using namespace ESPressio;
namespace S=ESPressio::State;
struct DynamicValue final {
    std::uint32_t Value=0;
    constexpr bool operator==(const DynamicValue& other) const noexcept{return Value==other.Value;}
    ESPRESSIO_SERIALIZABLE_TYPE(DynamicValue)
    ESPRESSIO_SERIALIZABLE_SCHEMA_VERSION(1)
    ESPRESSIO_SERIALIZABLE_PROPERTIES(ESPRESSIO_PROPERTY("value",Value))
};
struct DynamicState final:S::SerializableState<DynamicState,DynamicValue>{
    static constexpr S::StateTypeId TypeId{0x5601};
    static constexpr std::string_view CanonicalName="Test.State.DynamicRead";
};
static System::DeviceRuntimeIdentity Identity(){
    System::DeviceIdentifier::Storage bytes{};bytes[0]=1;
    return {System::DeviceIdentifier{bytes},System::RuntimeIncarnationId{1}};
}
static Timing::QualifiedTime Capture(){return {99,Timing::TimeReliability::Holdover};}
int main(){
    assert(System::RuntimeIdentity::Install(Identity())==System::RuntimeIdentity::InstallationStatus::Success);
    Primitive::TypeDirectory<1> directory;
    assert(directory.Register<DynamicState>()==Primitive::TypeDirectoryRegistrationStatus::Success);
    assert(directory.Initialize()==Primitive::TypeDirectoryInitializationStatus::Success);
    const auto* common=directory.View().Find({S::StateFamilyId,DynamicState::TypeId.Value()});assert(common);
    const auto* descriptor=S::GetStateTypeDescriptor(*common);assert(descriptor);
    std::array<std::uint8_t,Serializable::MaximumSerializedSize<DynamicValue,Serializable::DirectBinary>> bytes{};
    auto absent=S::ReadDynamicState(*descriptor,S::StatePayloadFormat::DirectBinary,bytes.data(),bytes.size());
    assert(absent.Status==S::StateDynamicReadStatus::NoValue);
    S::Runtime<S::TypeConfiguration<DynamicState>> runtime;
    auto owner=runtime.BindOwner<DynamicState>();assert(owner);
    assert(runtime.Initialize(directory.View(),&Capture)==S::StateRuntimeStatus::Success);
    assert(runtime.Start()==S::StateRuntimeStatus::Success);
    assert(owner.Set({42})==S::StateSetStatus::Changed);
    auto read=S::ReadDynamicState(*descriptor,S::StatePayloadFormat::DirectBinary,bytes.data(),bytes.size());
    assert(read && read.Bytes>0 && read.TruthTime.Nanoseconds==99 && read.TruthTime.Reliability==Timing::TimeReliability::Holdover);
    DynamicValue decoded{};
    assert(Serializable::DeserializeBoundedDirectBinary(bytes.data(),read.Bytes,decoded));
    assert(decoded.Value==42);
    auto shortRead=S::ReadDynamicState(*descriptor,S::StatePayloadFormat::DirectBinary,bytes.data(),0);
    assert(shortRead.Status==S::StateDynamicReadStatus::InsufficientOutput);
    assert(runtime.Shutdown()==S::StateRuntimeStatus::Success);
}
