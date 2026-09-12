#pragma once
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <ESPressio_BoundedCborArchive.hpp>
#include <ESPressio_BoundedDeserializer.hpp>
#include <ESPressio_BoundedJsonArchive.hpp>
#include <ESPressio_RuntimeIdentity.hpp>
#include "ESPressio_StateSnapshot.hpp"
#include "ESPressio_StateVersion.hpp"
#include "ESPressio_TransmissibleState.hpp"

namespace ESPressio::State {

enum class StateMessageKind : std::uint8_t {
    Publication=1,
    PublicationAccepted=2,
    SubscribeRequest=3,
    SubscribeSnapshot=4,
    SubscribeNoValue=5,
    SubscribeAccepted=6,
    SubscribeRejected=7,
    UnsubscribeRequest=8,
    BaselineSnapshot=9,
    BaselineAccepted=10,
    ResyncRequest=11,
    ResyncSnapshot=12,
    ResyncAccepted=13,
    ResyncRequired=14
};

constexpr bool IsValidStateMessageKind(StateMessageKind kind) noexcept {
    const auto value=static_cast<std::uint8_t>(kind);
    return value>=static_cast<std::uint8_t>(StateMessageKind::Publication) &&
           value<=static_cast<std::uint8_t>(StateMessageKind::ResyncRequired);
}
constexpr bool IsStateSnapshotControlKind(StateMessageKind kind) noexcept {
    return kind==StateMessageKind::SubscribeSnapshot || kind==StateMessageKind::BaselineSnapshot || kind==StateMessageKind::ResyncSnapshot;
}
constexpr bool IsStateAcceptanceControlKind(StateMessageKind kind) noexcept {
    return kind==StateMessageKind::PublicationAccepted || kind==StateMessageKind::BaselineAccepted || kind==StateMessageKind::ResyncAccepted;
}
constexpr bool IsStateCommonOnlyControlKind(StateMessageKind kind) noexcept {
    return IsValidStateMessageKind(kind) && kind!=StateMessageKind::Publication &&
           !IsStateSnapshotControlKind(kind) && !IsStateAcceptanceControlKind(kind);
}
constexpr bool StateControlUsesResyncToken(StateMessageKind kind) noexcept {
    return kind==StateMessageKind::ResyncRequest || kind==StateMessageKind::ResyncSnapshot || kind==StateMessageKind::ResyncAccepted;
}

inline constexpr std::size_t StatePublicationWireHeaderSize=73;
inline constexpr std::size_t StateControlWireHeaderSize=62;
inline constexpr std::size_t StateSnapshotControlWireHeaderSize=78;
inline constexpr std::size_t StateAcceptanceControlWireHeaderSize=65;

enum class StateWireStatus : std::uint8_t {
    Success,
    InvalidHeader,
    UnsupportedProtocol,
    InvalidLength,
    PayloadTooLarge,
    SchemaOrDecodeFailure,
    InsufficientOutput
};
struct StateWireResult final {
    StateWireStatus Status=StateWireStatus::InvalidHeader;
    std::size_t Bytes=0;
    constexpr explicit operator bool() const noexcept { return Status==StateWireStatus::Success; }
};

struct StatePublicationWireHeader final {
    StateTypeId TypeId{};
    System::DeviceRuntimeIdentity Owner{};
    System::DeviceRuntimeIdentity Requester{};
    StateSessionToken Session{};
    StateVersion Version{};
    Timing::QualifiedTime TruthTime{};
    std::uint32_t PayloadLength=0;
};
struct StateControlWireHeader final {
    StateMessageKind Kind=StateMessageKind::SubscribeRequest;
    StateTypeId TypeId{};
    System::DeviceRuntimeIdentity Owner{};
    System::DeviceRuntimeIdentity Requester{};
    StateSessionToken Session{};
    StateResyncToken Resync{};
    std::uint8_t ControlCode=0;
};
struct StateSnapshotControlWireHeader final {
    StateControlWireHeader Control{};
    StateVersion Version{};
    Timing::QualifiedTime TruthTime{};
    std::uint32_t PayloadLength=0;
};
struct StateAcceptanceControlWireHeader final {
    StateControlWireHeader Control{};
    StateVersion Version{};
};

namespace Detail {
inline void WriteStateLE(std::uint8_t* output,std::uint64_t value,std::size_t bytes) noexcept {
    for(std::size_t i=0;i<bytes;++i){output[i]=static_cast<std::uint8_t>(value);value>>=8;}
}
inline std::uint64_t ReadStateLE(const std::uint8_t* input,std::size_t bytes) noexcept {
    std::uint64_t value=0;
    for(std::size_t i=0;i<bytes;++i) value|=std::uint64_t(input[i])<<(8*i);
    return value;
}
inline void WriteStateIdentity(std::uint8_t* output,const System::DeviceRuntimeIdentity& identity) noexcept {
    for(std::size_t i=0;i<System::DeviceIdentifier::Size;++i) output[i]=identity.Device.Bytes()[i];
    WriteStateLE(output+16,identity.Incarnation.Value(),4);
}
inline System::DeviceRuntimeIdentity ReadStateIdentity(const std::uint8_t* input) noexcept {
    System::DeviceIdentifier::Storage bytes{};
    for(std::size_t i=0;i<System::DeviceIdentifier::Size;++i) bytes[i]=input[i];
    return {System::DeviceIdentifier{bytes},System::RuntimeIncarnationId{static_cast<std::uint32_t>(ReadStateLE(input+16,4))}};
}
inline bool ValidateControlSemanticFields(const StateControlWireHeader& header) noexcept {
    if(!IsValidStateMessageKind(header.Kind) || header.Kind==StateMessageKind::Publication || !header.TypeId ||
       !header.Owner.Device || !header.Requester || !header.Session) return false;
    if(header.Kind!=StateMessageKind::SubscribeRequest && !header.Owner.Incarnation) return false;
    if(StateControlUsesResyncToken(header.Kind)!=bool(header.Resync)) return false;
    return true;
}
template<class Value,class Format> constexpr std::size_t StateValueMaximum() noexcept {
    static_assert(Serializable::IsBoundedSerializable<Value>,"State wire Value requires a bounded P3 object schema");
    return Serializable::MaximumSerializedSize<Value,Format>;
}
template<class Value,class Format>
Serializable::BoundedSerializationResult SerializeStateValue(const Value& value,std::uint8_t* output,std::size_t capacity){
    if constexpr(std::is_same_v<Format,Serializable::DirectBinary>) return Serializable::SerializeDirectBinary(value,output,capacity);
    else if constexpr(std::is_same_v<Format,Serializable::CBOR>) return Serializable::SerializeBoundedCbor(value,output,capacity);
    else { static_assert(std::is_same_v<Format,Serializable::JSON>,"Unsupported State P3 format");return Serializable::SerializeBoundedJson(value,output,capacity); }
}
template<class Value,class Format>
Serializable::BoundedSerializationResult DeserializeStateValue(const std::uint8_t* data,std::size_t size,Value& value){
    if constexpr(std::is_same_v<Format,Serializable::DirectBinary>) return Serializable::DeserializeBoundedDirectBinary(data,size,value);
    else if constexpr(std::is_same_v<Format,Serializable::CBOR>) return Serializable::DeserializeBoundedCbor(data,size,value);
    else { static_assert(std::is_same_v<Format,Serializable::JSON>,"Unsupported State P3 format");return Serializable::DeserializeBoundedJson(data,size,value); }
}
}

template<class TState,class Format>
inline constexpr std::size_t MaximumCompleteStatePublicationWireBytes=[] {
    static_assert(TState::IsTransmissibleState && TState::ValidateTier());
    constexpr auto payload=Detail::StateValueMaximum<typename TState::ValueType,Format>();
    static_assert(payload<=UINT32_MAX && payload<=SIZE_MAX-StatePublicationWireHeaderSize);
    return StatePublicationWireHeaderSize+payload;
}();
template<class TState,class Format>
inline constexpr std::size_t MaximumCompleteStateSnapshotControlWireBytes=[] {
    static_assert(TState::IsTransmissibleState && TState::ValidateTier());
    constexpr auto payload=Detail::StateValueMaximum<typename TState::ValueType,Format>();
    static_assert(payload<=UINT32_MAX && payload<=SIZE_MAX-StateSnapshotControlWireHeaderSize);
    return StateSnapshotControlWireHeaderSize+payload;
}();

inline StateWireResult EncodeStatePublicationHeader(const StatePublicationWireHeader& header,std::uint8_t* output,std::size_t capacity) noexcept {
    if(!header.TypeId || !header.Owner || !header.Requester || !header.Session || !header.Version ||
       !Timing::IsValidTimeReliability(header.TruthTime.Reliability)) return {};
    if(!output || capacity<StatePublicationWireHeaderSize) return {StateWireStatus::InsufficientOutput,0};
    Detail::WriteStateLE(output+0,StateFamilyId,2);
    Detail::WriteStateLE(output+2,StateProtocolVersion,2);
    output[4]=static_cast<std::uint8_t>(StateMessageKind::Publication);
    Detail::WriteStateLE(output+5,header.TypeId.Value(),8);
    Detail::WriteStateIdentity(output+13,header.Owner);
    Detail::WriteStateIdentity(output+33,header.Requester);
    Detail::WriteStateLE(output+53,header.Session.Value(),4);
    output[57]=header.Version.Phase?1:0;
    Detail::WriteStateLE(output+58,header.Version.Revision,2);
    Detail::WriteStateLE(output+60,header.TruthTime.Nanoseconds,8);
    output[68]=static_cast<std::uint8_t>(header.TruthTime.Reliability);
    Detail::WriteStateLE(output+69,header.PayloadLength,4);
    return {StateWireStatus::Success,StatePublicationWireHeaderSize};
}

inline StateWireResult DecodeStatePublicationHeader(const std::uint8_t* data,std::size_t size,StatePublicationWireHeader& output) noexcept {
    if(!data || size<StatePublicationWireHeaderSize || Detail::ReadStateLE(data,2)!=StateFamilyId ||
       data[4]!=static_cast<std::uint8_t>(StateMessageKind::Publication)) return {};
    if(Detail::ReadStateLE(data+2,2)!=StateProtocolVersion) return {StateWireStatus::UnsupportedProtocol,0};
    StatePublicationWireHeader header;
    header.TypeId=StateTypeId{Detail::ReadStateLE(data+5,8)};
    header.Owner=Detail::ReadStateIdentity(data+13);
    header.Requester=Detail::ReadStateIdentity(data+33);
    header.Session=StateSessionToken{static_cast<std::uint32_t>(Detail::ReadStateLE(data+53,4))};
    if(data[57]>1) return {};
    header.Version={data[57]!=0,static_cast<std::uint16_t>(Detail::ReadStateLE(data+58,2))};
    header.TruthTime={Detail::ReadStateLE(data+60,8),static_cast<Timing::TimeReliability>(data[68])};
    header.PayloadLength=static_cast<std::uint32_t>(Detail::ReadStateLE(data+69,4));
    if(!header.TypeId || !header.Owner || !header.Requester || !header.Session || !header.Version ||
       !Timing::IsValidTimeReliability(header.TruthTime.Reliability)) return {};
    if(size-StatePublicationWireHeaderSize!=header.PayloadLength) return {StateWireStatus::InvalidLength,0};
    output=header;return {StateWireStatus::Success,size};
}

inline StateWireResult EncodeStateControlPrefix(const StateControlWireHeader& header,std::uint8_t* output,std::size_t capacity) noexcept {
    if(!Detail::ValidateControlSemanticFields(header)) return {};
    if(!output || capacity<StateControlWireHeaderSize) return {StateWireStatus::InsufficientOutput,0};
    Detail::WriteStateLE(output+0,StateFamilyId,2);
    Detail::WriteStateLE(output+2,StateProtocolVersion,2);
    output[4]=static_cast<std::uint8_t>(header.Kind);
    Detail::WriteStateLE(output+5,header.TypeId.Value(),8);
    Detail::WriteStateIdentity(output+13,header.Owner);
    Detail::WriteStateIdentity(output+33,header.Requester);
    Detail::WriteStateLE(output+53,header.Session.Value(),4);
    Detail::WriteStateLE(output+57,header.Resync.Value(),4);
    output[61]=header.ControlCode;
    return {StateWireStatus::Success,StateControlWireHeaderSize};
}

inline StateWireResult DecodeStateControlPrefix(const std::uint8_t* data,std::size_t size,StateControlWireHeader& output) noexcept {
    if(!data || size<StateControlWireHeaderSize || Detail::ReadStateLE(data,2)!=StateFamilyId) return {};
    if(Detail::ReadStateLE(data+2,2)!=StateProtocolVersion) return {StateWireStatus::UnsupportedProtocol,0};
    StateControlWireHeader header;
    header.Kind=static_cast<StateMessageKind>(data[4]);
    header.TypeId=StateTypeId{Detail::ReadStateLE(data+5,8)};
    header.Owner=Detail::ReadStateIdentity(data+13);
    header.Requester=Detail::ReadStateIdentity(data+33);
    header.Session=StateSessionToken{static_cast<std::uint32_t>(Detail::ReadStateLE(data+53,4))};
    header.Resync=StateResyncToken{static_cast<std::uint32_t>(Detail::ReadStateLE(data+57,4))};
    header.ControlCode=data[61];
    if(!Detail::ValidateControlSemanticFields(header)) return {};
    output=header;return {StateWireStatus::Success,StateControlWireHeaderSize};
}

inline StateWireResult EncodeStateControl(const StateControlWireHeader& header,std::uint8_t* output,std::size_t capacity) noexcept {
    if(!IsStateCommonOnlyControlKind(header.Kind)) return {};
    return EncodeStateControlPrefix(header,output,capacity);
}
inline StateWireResult DecodeStateControl(const std::uint8_t* data,std::size_t size,StateControlWireHeader& output) noexcept {
    StateControlWireHeader header;
    const auto decoded=DecodeStateControlPrefix(data,size,header);
    if(!decoded || !IsStateCommonOnlyControlKind(header.Kind)) return {};
    if(size!=StateControlWireHeaderSize) return {StateWireStatus::InvalidLength,0};
    output=header;return {StateWireStatus::Success,size};
}

inline StateWireResult EncodeStateSnapshotControlHeader(const StateSnapshotControlWireHeader& header,std::uint8_t* output,std::size_t capacity) noexcept {
    if(!IsStateSnapshotControlKind(header.Control.Kind) || !header.Version ||
       !Timing::IsValidTimeReliability(header.TruthTime.Reliability)) return {};
    if(!output || capacity<StateSnapshotControlWireHeaderSize) return {StateWireStatus::InsufficientOutput,0};
    const auto prefix=EncodeStateControlPrefix(header.Control,output,capacity);if(!prefix) return prefix;
    output[62]=header.Version.Phase?1:0;
    Detail::WriteStateLE(output+63,header.Version.Revision,2);
    Detail::WriteStateLE(output+65,header.TruthTime.Nanoseconds,8);
    output[73]=static_cast<std::uint8_t>(header.TruthTime.Reliability);
    Detail::WriteStateLE(output+74,header.PayloadLength,4);
    return {StateWireStatus::Success,StateSnapshotControlWireHeaderSize};
}
inline StateWireResult DecodeStateSnapshotControlHeader(const std::uint8_t* data,std::size_t size,StateSnapshotControlWireHeader& output) noexcept {
    StateControlWireHeader control;
    const auto prefix=DecodeStateControlPrefix(data,size,control);if(!prefix || !IsStateSnapshotControlKind(control.Kind)) return {};
    if(size<StateSnapshotControlWireHeaderSize || data[62]>1) return {};
    StateSnapshotControlWireHeader header;
    header.Control=control;
    header.Version={data[62]!=0,static_cast<std::uint16_t>(Detail::ReadStateLE(data+63,2))};
    header.TruthTime={Detail::ReadStateLE(data+65,8),static_cast<Timing::TimeReliability>(data[73])};
    header.PayloadLength=static_cast<std::uint32_t>(Detail::ReadStateLE(data+74,4));
    if(!header.Version || !Timing::IsValidTimeReliability(header.TruthTime.Reliability)) return {};
    if(size-StateSnapshotControlWireHeaderSize!=header.PayloadLength) return {StateWireStatus::InvalidLength,0};
    output=header;return {StateWireStatus::Success,size};
}

inline StateWireResult EncodeStateAcceptanceControl(const StateAcceptanceControlWireHeader& header,std::uint8_t* output,std::size_t capacity) noexcept {
    if(!IsStateAcceptanceControlKind(header.Control.Kind) || !header.Version) return {};
    if(!output || capacity<StateAcceptanceControlWireHeaderSize) return {StateWireStatus::InsufficientOutput,0};
    const auto prefix=EncodeStateControlPrefix(header.Control,output,capacity);if(!prefix) return prefix;
    output[62]=header.Version.Phase?1:0;
    Detail::WriteStateLE(output+63,header.Version.Revision,2);
    return {StateWireStatus::Success,StateAcceptanceControlWireHeaderSize};
}
inline StateWireResult DecodeStateAcceptanceControl(const std::uint8_t* data,std::size_t size,StateAcceptanceControlWireHeader& output) noexcept {
    if(size!=StateAcceptanceControlWireHeaderSize) return {StateWireStatus::InvalidLength,0};
    StateControlWireHeader control;
    const auto prefix=DecodeStateControlPrefix(data,size,control);if(!prefix || !IsStateAcceptanceControlKind(control.Kind) || data[62]>1) return {};
    StateAcceptanceControlWireHeader header{control,{data[62]!=0,static_cast<std::uint16_t>(Detail::ReadStateLE(data+63,2))}};
    if(!header.Version) return {};
    output=header;return {StateWireStatus::Success,size};
}

template<class TState,class Format>
StateWireResult EncodeStatePublication(const StateSnapshot<TState>& snapshot,StateVersion version,
                                       const System::DeviceRuntimeIdentity& owner,const System::DeviceRuntimeIdentity& requester,
                                       StateSessionToken session,std::uint8_t* output,std::size_t capacity){
    static_assert(TState::IsTransmissibleState && TState::ValidateTier());
    if(!output || capacity<StatePublicationWireHeaderSize) return {StateWireStatus::InsufficientOutput,0};
    auto payload=Detail::SerializeStateValue<typename TState::ValueType,Format>(snapshot.Value,output+StatePublicationWireHeaderSize,capacity-StatePublicationWireHeaderSize);
    if(!payload) return {StateWireStatus::SchemaOrDecodeFailure,0};
    if(payload.Bytes>UINT32_MAX) return {StateWireStatus::PayloadTooLarge,0};
    auto header=EncodeStatePublicationHeader({TState::TypeId,owner,requester,session,version,snapshot.TruthTime,static_cast<std::uint32_t>(payload.Bytes)},output,capacity);
    if(!header) return header;
    return {StateWireStatus::Success,StatePublicationWireHeaderSize+payload.Bytes};
}

template<class TState,class Format>
StateWireResult DecodeStatePublicationValue(const StatePublicationWireHeader& header,const std::uint8_t* payload,std::size_t size,typename TState::ValueType& output){
    static_assert(TState::IsTransmissibleState && TState::ValidateTier());
    if(header.TypeId!=TState::TypeId || header.PayloadLength!=size) return {};
    if(size>Detail::StateValueMaximum<typename TState::ValueType,Format>()) return {StateWireStatus::PayloadTooLarge,0};
    auto decoded=Detail::DeserializeStateValue<typename TState::ValueType,Format>(payload,size,output);
    return decoded ? StateWireResult{StateWireStatus::Success,size} : StateWireResult{StateWireStatus::SchemaOrDecodeFailure,0};
}

template<class TState,class Format>
StateWireResult EncodeStateSnapshotControl(const StateControlWireHeader& control,const StateSnapshot<TState>& snapshot,StateVersion version,
                                           std::uint8_t* output,std::size_t capacity){
    static_assert(TState::IsTransmissibleState && TState::ValidateTier());
    if(!IsStateSnapshotControlKind(control.Kind) || control.TypeId!=TState::TypeId) return {};
    if(!output || capacity<StateSnapshotControlWireHeaderSize) return {StateWireStatus::InsufficientOutput,0};
    auto payload=Detail::SerializeStateValue<typename TState::ValueType,Format>(snapshot.Value,output+StateSnapshotControlWireHeaderSize,capacity-StateSnapshotControlWireHeaderSize);
    if(!payload) return {StateWireStatus::SchemaOrDecodeFailure,0};
    if(payload.Bytes>UINT32_MAX) return {StateWireStatus::PayloadTooLarge,0};
    auto header=EncodeStateSnapshotControlHeader({control,version,snapshot.TruthTime,static_cast<std::uint32_t>(payload.Bytes)},output,capacity);
    if(!header) return header;
    return {StateWireStatus::Success,StateSnapshotControlWireHeaderSize+payload.Bytes};
}

template<class TState,class Format>
StateWireResult DecodeStateSnapshotControlValue(const StateSnapshotControlWireHeader& header,const std::uint8_t* payload,std::size_t size,typename TState::ValueType& output){
    static_assert(TState::IsTransmissibleState && TState::ValidateTier());
    if(header.Control.TypeId!=TState::TypeId || header.PayloadLength!=size) return {};
    if(size>Detail::StateValueMaximum<typename TState::ValueType,Format>()) return {StateWireStatus::PayloadTooLarge,0};
    auto decoded=Detail::DeserializeStateValue<typename TState::ValueType,Format>(payload,size,output);
    return decoded ? StateWireResult{StateWireStatus::Success,size} : StateWireResult{StateWireStatus::SchemaOrDecodeFailure,0};
}

} // namespace ESPressio::State
