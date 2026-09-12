#include <ESPressio_States.hpp>
#include <ESPressio_SerializationMacros.hpp>
#include <string>
#include <string_view>
namespace S=ESPressio::State;
struct UnboundedValue final {
    std::string Text;
    ESPRESSIO_SERIALIZABLE_TYPE(UnboundedValue)
    ESPRESSIO_SERIALIZABLE_SCHEMA_VERSION(1)
    ESPRESSIO_SERIALIZABLE_PROPERTIES(ESPRESSIO_PROPERTY("text",Text))
};
namespace ESPressio::State {
template<> struct StateStorageTraits<UnboundedValue> {
    static constexpr bool Supported=true;
    static bool Prepare(const UnboundedValue& value,UnboundedValue& prepared) noexcept { prepared=value;return true; }
    static void Commit(UnboundedValue& value,const UnboundedValue& prepared) noexcept { value=prepared; }
    static void CopyOut(const UnboundedValue& value,UnboundedValue& output) noexcept { output=value; }
};
}
struct InvalidSerializableState final : S::SerializableState<InvalidSerializableState,UnboundedValue> {
    static constexpr S::StateTypeId TypeId{4001};
    static constexpr std::string_view CanonicalName="CompileFail.State.Unbounded";
};
auto descriptor=InvalidSerializableState::GetPrimitiveTypeDescriptor();
int main(){return descriptor.Key.TypeValue==0;}
