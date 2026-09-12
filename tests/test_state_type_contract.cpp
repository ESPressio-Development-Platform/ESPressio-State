#include <ESPressio_States.hpp>
#include <ESPressio_TypeDirectory.hpp>
#include <cassert>
#include <string_view>
#include <type_traits>
using namespace ESPressio;
namespace S=ESPressio::State;
struct LocalFlag final : S::State<LocalFlag,bool> {
    static constexpr S::StateTypeId TypeId{1001};
    static constexpr std::string_view CanonicalName="Test.State.LocalFlag";
};
int main(){
    static_assert(std::is_same_v<decltype(LocalFlag::TypeId),const S::StateTypeId>);
    static_assert(bool(LocalFlag::TypeId));
    static_assert(!LocalFlag::IsSerializableState && !LocalFlag::IsTransmissibleState);
    Primitive::TypeDirectory<1> directory;
    assert(directory.Register<LocalFlag>()==Primitive::TypeDirectoryRegistrationStatus::Success);
    assert(directory.Initialize()==Primitive::TypeDirectoryInitializationStatus::Success);
    const auto* common=directory.View().Find({S::StateFamilyId,LocalFlag::TypeId.Value()});
    assert(common && common->CanonicalName==LocalFlag::CanonicalName);
    const auto* state=S::GetStateTypeDescriptor(*common);
    assert(state && state->TypeId==LocalFlag::TypeId && state->Tier==S::StateTier::Local);
    assert(state->ValueBytes==sizeof(bool));
}
