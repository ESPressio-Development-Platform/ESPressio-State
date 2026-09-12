#pragma once
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <ESPressio_ContractFingerprintBuilder.hpp>
#include <ESPressio_PrimitiveTypeDescriptor.hpp>
#include "ESPressio_StateTypeRuntime.hpp"
namespace ESPressio::State {
struct StateTypeDescriptor final {
    StateTypeId TypeId{};
    StateTier Tier=StateTier::Local;
    std::size_t ValueBytes=0;
    std::size_t RuntimeBytes=0;
    bool (*HasValue)() noexcept=nullptr;
    bool (*HasOwner)() noexcept=nullptr;
};
inline const StateTypeDescriptor* GetStateTypeDescriptor(const Primitive::PrimitiveTypeDescriptor& common) noexcept {
    if(common.Key.Family!=StateFamilyId || !common.FamilyExtension.Data) return nullptr;
    auto* extension=static_cast<const StateTypeDescriptor*>(common.FamilyExtension.Data);
    return extension->TypeId.Value()==common.Key.TypeValue ? extension : nullptr;
}
template<class T> struct StateDescriptorProvider final {
    static Primitive::PrimitiveTypeDescriptor Describe() noexcept {
        static_assert(Detail::ValidateLocalStateType<T>());
        static_assert(!T::IsSerializableState,
                      "Serializable/Transmissible State descriptor activation is introduced by S5-07 P3 integration");
        static const StateTypeDescriptor extension={T::TypeId,StateTier::Local,sizeof(typename T::ValueType),sizeof(StateTypeRuntime<T>),
            []() noexcept {return StateTypeRuntime<T>::Get().HasValue();},
            []() noexcept {return StateTypeRuntime<T>::Get().HasOwner();}};
        Primitive::ContractFingerprintBuilder fingerprint;
        fingerprint.Text("ESPressio.State.Contract.v1");
        fingerprint.Integer(StateFamilyId);fingerprint.Integer(T::TypeId.Value());
        fingerprint.Byte(static_cast<std::uint8_t>(StateTier::Local));
        return {{StateFamilyId,T::TypeId.Value()},T::CanonicalName,Primitive::PrimitiveTypeCapabilities{},
            {1,1},fingerprint.Finish(),{0},{&extension}};
    }
};
}
