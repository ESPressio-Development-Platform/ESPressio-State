#pragma once
#include <cstddef>
#include <cstdint>
#include <ESPressio_TypeDirectory.hpp>
#include "ESPressio_StateDescriptor.hpp"
#include "ESPressio_StateRuntime.hpp"

namespace ESPressio::State {

/// <summary>Immutable P1 metadata view for one registered State Type.</summary>
/// <remarks>The pointers refer to the frozen caller-owned TypeDirectory and static State family
/// extension. No runtime value, owner/session occupancy or mutable registry state is copied here.</remarks>
struct StateTypeIntrospectionEntry final {
    const Primitive::PrimitiveTypeDescriptor* Common=nullptr;
    const StateTypeDescriptor* State=nullptr;
    constexpr explicit operator bool() const noexcept { return Common && State; }
};

enum class StateTypeEnumerationStatus : std::uint8_t {
    Success,
    InvalidDirectory,
    InsufficientOutput
};
struct StateTypeEnumerationResult final {
    StateTypeEnumerationStatus Status=StateTypeEnumerationStatus::InvalidDirectory;
    std::size_t Count=0;
    std::size_t Required=0;
    constexpr explicit operator bool() const noexcept { return Status==StateTypeEnumerationStatus::Success; }
};

/// <summary>Enumerates the frozen State subset of a P1 Primitive TypeDirectory into caller storage.</summary>
/// <remarks>Registration order has no semantics; the directory's deterministic Family+TypeId order
/// is retained. Insufficient output reports the complete required count while copying only Capacity
/// entries. No allocation, mutable parallel registry or callback is involved.</remarks>
inline StateTypeEnumerationResult EnumerateStateTypes(Primitive::TypeDirectoryView directory,
                                                       StateTypeIntrospectionEntry* output,
                                                       std::size_t capacity) noexcept {
    if(!directory.IsFrozen()) return {};
    std::size_t count=0,required=0;
    for(const auto& common:directory) {
        if(common.Key.Family!=StateFamilyId) continue;
        const auto* state=GetStateTypeDescriptor(common);
        if(!state) continue;
        if(count<capacity && output) output[count++]={&common,state};
        ++required;
    }
    return {required<=capacity?StateTypeEnumerationStatus::Success:StateTypeEnumerationStatus::InsufficientOutput,
            count,required};
}

/// <summary>Finds immutable State family metadata by runtime StateTypeId in a frozen P1 directory.</summary>
inline StateTypeIntrospectionEntry FindStateType(Primitive::TypeDirectoryView directory,StateTypeId typeId) noexcept {
    if(!directory.IsFrozen() || !typeId) return {};
    const auto* common=directory.Find({StateFamilyId,typeId.Value()});
    if(!common) return {};
    const auto* state=GetStateTypeDescriptor(*common);
    return state?StateTypeIntrospectionEntry{common,state}:StateTypeIntrospectionEntry{};
}

enum class StateDynamicRemoteReadStatus : std::uint8_t {
    Success,
    UnknownType,
    NotTransmissible,
    NoValue,
    InsufficientOutput,
    SerializationFailure,
    UnsupportedFormat
};
struct StateDynamicRemoteReadResult final {
    StateDynamicRemoteReadStatus Status=StateDynamicRemoteReadStatus::UnknownType;
    std::size_t Bytes=0;
    Timing::QualifiedTime TruthTime{};
    StateRemoteSessionState Session=StateRemoteSessionState::Inactive;
    constexpr explicit operator bool() const noexcept { return Status==StateDynamicRemoteReadStatus::Success; }
};

namespace Detail {
template<class TState,class Format>
StateDynamicRemoteReadResult SerializeDynamicRemoteSnapshot(const StateSnapshot<TState>& snapshot,
                                                             StateRemoteSessionState session,
                                                             std::uint8_t* output,std::size_t capacity) {
    using Value=typename TState::ValueType;
    constexpr auto maximum=Serializable::MaximumSerializedSize<Value,Format>;
    const auto encoded=SerializeStateValue<Value,Format>(snapshot.Value,output,capacity);
    if(!encoded) return {capacity<maximum?StateDynamicRemoteReadStatus::InsufficientOutput:
                                         StateDynamicRemoteReadStatus::SerializationFailure,
                         0,snapshot.TruthTime,session};
    return {StateDynamicRemoteReadStatus::Success,encoded.Bytes,snapshot.TruthTime,session};
}

template<class TState,class... TConfigurations>
StateDynamicRemoteReadResult ReadDynamicRemoteType(const Runtime<TConfigurations...>& runtime,
                                                    const System::DeviceIdentifier& owner,
                                                    StatePayloadFormat format,
                                                    std::uint8_t* output,std::size_t capacity) {
    if constexpr(!TState::IsTransmissibleState) {
        (void)runtime;(void)owner;(void)format;(void)output;(void)capacity;
        return {StateDynamicRemoteReadStatus::NotTransmissible};
    } else {
        StateSnapshot<TState> snapshot{};
        const auto session=runtime.template GetRemoteSessionStatus<TState>(owner);
        if(!runtime.template TryReadRemote<TState>(owner,snapshot))
            return {StateDynamicRemoteReadStatus::NoValue,0,{},session};
        switch(format) {
            case StatePayloadFormat::DirectBinary:
                return SerializeDynamicRemoteSnapshot<TState,Serializable::DirectBinary>(snapshot,session,output,capacity);
            case StatePayloadFormat::CBOR:
                return SerializeDynamicRemoteSnapshot<TState,Serializable::CBOR>(snapshot,session,output,capacity);
            case StatePayloadFormat::JSON:
                return SerializeDynamicRemoteSnapshot<TState,Serializable::JSON>(snapshot,session,output,capacity);
        }
        return {StateDynamicRemoteReadStatus::UnsupportedFormat,0,snapshot.TruthTime,session};
    }
}
}

/// <summary>Performs a bounded read-only remote State lookup through the Runtime's static Type pack.</summary>
/// <remarks>The Primitive directory remains metadata-only: this function statically dispatches over
/// the Runtime's configured Types and copies only the retained last-known StateSnapshot into the
/// caller's bounded serialized buffer. It exposes no SetByTypeId and no reachability/freshness verdict.</remarks>
template<class... TConfigurations>
StateDynamicRemoteReadResult ReadDynamicRemoteState(const Runtime<TConfigurations...>& runtime,
                                                     StateTypeId typeId,
                                                     const System::DeviceIdentifier& owner,
                                                     StatePayloadFormat format,
                                                     std::uint8_t* output,std::size_t capacity) {
    if(!typeId || !owner) return {StateDynamicRemoteReadStatus::UnknownType};
    StateDynamicRemoteReadResult result{StateDynamicRemoteReadStatus::UnknownType};
    bool matched=false;
    auto tryType=[&](auto configuration) {
        using C=decltype(configuration);
        using T=typename C::StateType;
        if(!matched && T::TypeId==typeId) {
            matched=true;
            result=Detail::ReadDynamicRemoteType<T>(runtime,owner,format,output,capacity);
        }
    };
    (tryType(TConfigurations{}),...);
    return result;
}

} // namespace ESPressio::State
