#include <ESPressio_States.hpp>
#include <ESPressio_SerializationMacros.hpp>
#include <ESPressio_TypeDirectory.hpp>
#include <array>
#include <cassert>
#include <cstdint>
#include <string_view>

using namespace ESPressio;
namespace S=ESPressio::State;

struct ToolValue final {
    std::uint32_t Reading=0;
    constexpr bool operator==(const ToolValue& other) const noexcept { return Reading==other.Reading; }
    ESPRESSIO_SERIALIZABLE_TYPE(ToolValue)
    ESPRESSIO_SERIALIZABLE_SCHEMA_VERSION(1)
    ESPRESSIO_SERIALIZABLE_PROPERTIES(ESPRESSIO_PROPERTY("reading",Reading))
};
struct ToolState final:S::SerializableState<ToolState,ToolValue> {
    static constexpr S::StateTypeId TypeId{0x1801};
    static constexpr std::string_view CanonicalName="Example.State.DynamicRead";
};

Primitive::TypeDirectory<1> directory;
S::Runtime<S::TypeConfiguration<ToolState>> states;
S::StateOwner<ToolState> owner;

void setup() {
    (void)directory.Register<ToolState>();
    (void)directory.Initialize();
    owner=states.BindOwner<ToolState>();
    (void)states.Initialize(directory.View());
    (void)states.Start();
    (void)owner.Set({456},{5000,Timing::TimeReliability::Synchronized});

    const auto* common=directory.View().Find({S::StateFamilyId,ToolState::TypeId.Value()});
    assert(common!=nullptr);
    const auto* stateDescriptor=S::GetStateTypeDescriptor(*common);
    assert(stateDescriptor!=nullptr);

    std::array<std::uint8_t,Serializable::MaximumSerializedSize<ToolValue,Serializable::DirectBinary>> bytes{};
    const auto read=S::ReadDynamicState(*stateDescriptor,S::StatePayloadFormat::DirectBinary,
                                        bytes.data(),bytes.size());
    assert(read && read.Bytes>0 && read.TruthTime.Nanoseconds==5000);

    ToolValue decoded{};
    assert(Serializable::DeserializeBoundedDirectBinary(bytes.data(),read.Bytes,decoded));
    assert(decoded.Reading==456);
}

void loop() {}
