#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <ESPressio_ContractFingerprintBuilder.hpp>
#include <ESPressio_PrimitivePolicy.hpp>
#include <ESPressio_PrimitiveTypeDescriptor.hpp>
#include <ESPressio_SchemaDescriptor.hpp>
#include "ESPressio_StateTypeRuntime.hpp"
#include "ESPressio_StateWireV1.hpp"
namespace ESPressio::State {

struct StateTypeResourceProfile final {
    std::size_t RuntimeBytes=0;
    std::size_t ValueBytes=0;
    std::size_t MaximumSerializedValueBytes=0;
    std::size_t MaximumPublicationWireBytes=0;
    std::size_t MaximumSnapshotControlWireBytes=0;
};

enum class StateDynamicReadStatus : std::uint8_t {
    Success,
    NoValue,
    InsufficientOutput,
    SerializationFailure,
    UnsupportedFormat
};
struct StateDynamicReadResult final {
    StateDynamicReadStatus Status=StateDynamicReadStatus::SerializationFailure;
    std::size_t Bytes=0;
    Timing::QualifiedTime TruthTime{};
    constexpr explicit operator bool() const noexcept { return Status==StateDynamicReadStatus::Success; }
};
using StateDynamicReadThunk=StateDynamicReadResult (*)(std::uint8_t*,std::size_t);

struct StateTypeDescriptor final {
    StateTypeId TypeId{};
    StateTier Tier=StateTier::Local;
    std::size_t ValueBytes=0;
    std::size_t RuntimeBytes=0;
    bool (*HasValue)() noexcept=nullptr;
    bool (*HasOwner)() noexcept=nullptr;
    const Serializable::StaticSchemaDescriptor* ValueSchema=nullptr;
    const Primitive::PrimitivePolicyDescriptor* ConvergencePolicy=nullptr;
    std::array<std::size_t,3> MaximumSerializedValueBytes{};
    std::array<std::size_t,3> MaximumPublicationWireBytes{};
    std::array<std::size_t,3> MaximumSnapshotControlWireBytes{};
    std::array<StateDynamicReadThunk,3> ReadValue{};
    StateTypeResourceProfile Resources{};
};
inline const StateTypeDescriptor* GetStateTypeDescriptor(const Primitive::PrimitiveTypeDescriptor& common) noexcept {
    if(common.Key.Family!=StateFamilyId || !common.FamilyExtension.Data) return nullptr;
    auto* extension=static_cast<const StateTypeDescriptor*>(common.FamilyExtension.Data);
    return extension->TypeId.Value()==common.Key.TypeValue ? extension : nullptr;
}

template<class T> struct StateDescriptorProvider final {
    template<class Format>
    static StateDynamicReadResult ReadValue(std::uint8_t* output,std::size_t capacity) {
        StateSnapshot<T> snapshot{};
        if(!StateTypeRuntime<T>::Get().TryRead(snapshot)) return {StateDynamicReadStatus::NoValue,0,{}};
        const auto encoded=Detail::SerializeStateValue<typename T::ValueType,Format>(snapshot.Value,output,capacity);
        if(!encoded) {
            return {capacity<Serializable::MaximumSerializedSize<typename T::ValueType,Format>
                        ? StateDynamicReadStatus::InsufficientOutput
                        : StateDynamicReadStatus::SerializationFailure,0,snapshot.TruthTime};
        }
        return {StateDynamicReadStatus::Success,encoded.Bytes,snapshot.TruthTime};
    }
    static Primitive::PrimitiveTypeDescriptor Describe() noexcept {
        static_assert(Detail::ValidateLocalStateType<T>());
        static const StateTypeDescriptor extension=[] {
            StateTypeDescriptor value{};
            value.TypeId=T::TypeId;
            value.Tier=T::IsTransmissibleState ? StateTier::Transmissible : (T::IsSerializableState ? StateTier::Serializable : StateTier::Local);
            value.ValueBytes=sizeof(typename T::ValueType);
            value.RuntimeBytes=sizeof(StateTypeRuntime<T>);
            value.HasValue=[]() noexcept {return StateTypeRuntime<T>::Get().HasValue();};
            value.HasOwner=[]() noexcept {return StateTypeRuntime<T>::Get().HasOwner();};
            if constexpr(T::IsSerializableState){
                static_assert(T::ValidateTier());
                using V=typename T::ValueType;
                value.ValueSchema=&Serializable::SchemaDescriptor<V>();
                value.MaximumSerializedValueBytes={
                    Serializable::MaximumSerializedSize<V,Serializable::DirectBinary>,
                    Serializable::MaximumSerializedSize<V,Serializable::CBOR>,
                    Serializable::MaximumSerializedSize<V,Serializable::JSON>};
                value.ReadValue={&ReadValue<Serializable::DirectBinary>,&ReadValue<Serializable::CBOR>,
                                 &ReadValue<Serializable::JSON>};
            }
            if constexpr(T::IsTransmissibleState){
                static_assert(T::ValidateTier());
                static const auto convergence=Primitive::PrimitivePolicyContract<typename T::ConvergencePolicy>::Descriptor();
                value.ConvergencePolicy=&convergence;
                value.MaximumPublicationWireBytes={
                    MaximumCompleteStatePublicationWireBytes<T,Serializable::DirectBinary>,
                    MaximumCompleteStatePublicationWireBytes<T,Serializable::CBOR>,
                    MaximumCompleteStatePublicationWireBytes<T,Serializable::JSON>};
                value.MaximumSnapshotControlWireBytes={
                    MaximumCompleteStateSnapshotControlWireBytes<T,Serializable::DirectBinary>,
                    MaximumCompleteStateSnapshotControlWireBytes<T,Serializable::CBOR>,
                    MaximumCompleteStateSnapshotControlWireBytes<T,Serializable::JSON>};
            }
            std::size_t serialized=0,publication=0,snapshot=0;
            for(auto bytes:value.MaximumSerializedValueBytes) if(bytes>serialized) serialized=bytes;
            for(auto bytes:value.MaximumPublicationWireBytes) if(bytes>publication) publication=bytes;
            for(auto bytes:value.MaximumSnapshotControlWireBytes) if(bytes>snapshot) snapshot=bytes;
            value.Resources={sizeof(StateTypeRuntime<T>),sizeof(typename T::ValueType),serialized,publication,snapshot};
            return value;
        }();

        std::size_t maximumWire=0;
        for(auto bytes:extension.MaximumSerializedValueBytes) if(bytes>maximumWire) maximumWire=bytes;
        for(auto bytes:extension.MaximumPublicationWireBytes) if(bytes>maximumWire) maximumWire=bytes;
        for(auto bytes:extension.MaximumSnapshotControlWireBytes) if(bytes>maximumWire) maximumWire=bytes;

        Primitive::ContractFingerprintBuilder fingerprint;
        fingerprint.Text("ESPressio.State.Contract.v1");
        fingerprint.Integer(StateFamilyId);fingerprint.Integer(T::TypeId.Value());
        fingerprint.Byte(static_cast<std::uint8_t>(extension.Tier));fingerprint.Integer(StateProtocolVersion);
        if constexpr(T::IsSerializableState) Serializable::WriteCanonicalSchema<typename T::ValueType>(fingerprint);
        if constexpr(T::IsTransmissibleState){
            fingerprint.Text("State.Publication.V1.LE.73");
            fingerprint.Text("State.Control.V1.LE.62.78.65");
            for(auto byte:extension.ConvergencePolicy->CanonicalBytes()) fingerprint.Byte(byte);
        }

        return {{StateFamilyId,T::TypeId.Value()},T::CanonicalName,
            Primitive::PrimitiveTypeCapabilities{T::IsTransmissibleState?std::uint8_t{3}:(T::IsSerializableState?std::uint8_t{1}:std::uint8_t{0})},
            {1,1},fingerprint.Finish(),{maximumWire},{&extension}};
    }
};

inline StateDynamicReadResult ReadDynamicState(const StateTypeDescriptor& descriptor,StatePayloadFormat format,
                                               std::uint8_t* output,std::size_t capacity) {
    const auto raw=static_cast<std::uint8_t>(format);
    if(raw<1 || raw>3 || !descriptor.ReadValue[raw-1])
        return {StateDynamicReadStatus::UnsupportedFormat,0,{}};
    return descriptor.ReadValue[raw-1](output,capacity);
}
}
