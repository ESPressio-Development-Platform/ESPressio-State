#include <ESPressio_States.hpp>
#include <ESPressio_SerializationMacros.hpp>
#include <ESPressio_TypeDirectory.hpp>
#include <array>
#include <cassert>
#include <cstdint>
#include <string_view>

using namespace ESPressio;
namespace S=ESPressio::State;

struct Sample final {
    std::uint32_t Reading=0;
    constexpr bool operator==(const Sample& other) const noexcept { return Reading==other.Reading; }
    ESPRESSIO_SERIALIZABLE_TYPE(Sample)
    ESPRESSIO_SERIALIZABLE_SCHEMA_VERSION(1)
    ESPRESSIO_SERIALIZABLE_PROPERTIES(ESPRESSIO_PROPERTY("reading",Reading))
};
struct SampleState final:S::SerializableState<SampleState,Sample> {
    static constexpr S::StateTypeId TypeId{0x1201};
    static constexpr std::string_view CanonicalName="Example.State.SerializableSample";
};

Primitive::TypeDirectory<1> directory;
S::Runtime<S::TypeConfiguration<SampleState>> states;
S::StateOwner<SampleState> owner;

void setup() {
    (void)directory.Register<SampleState>();
    (void)directory.Initialize();
    owner=states.BindOwner<SampleState>();
    (void)states.Initialize(directory.View());
    (void)states.Start();
    (void)owner.Set({37});

    S::StateSnapshot<SampleState> snapshot{};
    assert(states.TryRead(snapshot));

    std::array<std::uint8_t,Serializable::MaximumSerializedSize<Sample,Serializable::DirectBinary>> bytes{};
    const auto encoded=Serializable::SerializeDirectBinary(snapshot.Value,bytes.data(),bytes.size());
    assert(encoded && encoded.Bytes>0);
}

void loop() {}
