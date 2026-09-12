#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>
#include <ESPressio_BoundedCborArchive.hpp>
#include <ESPressio_BoundedDeserializer.hpp>
#include <ESPressio_BoundedJsonArchive.hpp>
#include <ESPressio_IAtomicRecordStore.hpp>
#include <ESPressio_RuntimeIdentity.hpp>
#include "ESPressio_StateSnapshot.hpp"
#include "ESPressio_StateTypes.hpp"

namespace ESPressio::State {

namespace PersistenceDetail {
inline constexpr std::size_t HeaderBytes=52;
inline constexpr std::uint8_t RecordVersion=1;

template<class Format> constexpr std::uint8_t FormatCode() noexcept {
    if constexpr(std::is_same_v<Format,Serializable::DirectBinary>) return 1;
    else if constexpr(std::is_same_v<Format,Serializable::CBOR>) return 2;
    else { static_assert(std::is_same_v<Format,Serializable::JSON>,"Unsupported State persistence format"); return 3; }
}
inline void WriteLE(std::uint8_t* output,std::uint64_t value,std::size_t bytes) noexcept {
    for(std::size_t i=0;i<bytes;++i){output[i]=static_cast<std::uint8_t>(value);value>>=8;}
}
inline std::uint64_t ReadLE(const std::uint8_t* input,std::size_t bytes) noexcept {
    std::uint64_t value=0;for(std::size_t i=0;i<bytes;++i)value|=std::uint64_t(input[i])<<(8*i);return value;
}
template<class Value,class Format>
Serializable::BoundedSerializationResult Serialize(const Value& value,std::uint8_t* output,std::size_t capacity){
    if constexpr(std::is_same_v<Format,Serializable::DirectBinary>) return Serializable::SerializeDirectBinary(value,output,capacity);
    else if constexpr(std::is_same_v<Format,Serializable::CBOR>) return Serializable::SerializeBoundedCbor(value,output,capacity);
    else { static_assert(std::is_same_v<Format,Serializable::JSON>,"Unsupported State persistence format"); return Serializable::SerializeBoundedJson(value,output,capacity); }
}
template<class Value,class Format>
Serializable::BoundedSerializationResult Deserialize(const std::uint8_t* data,std::size_t size,Value& value){
    if constexpr(std::is_same_v<Format,Serializable::DirectBinary>) return Serializable::DeserializeBoundedDirectBinary(data,size,value);
    else if constexpr(std::is_same_v<Format,Serializable::CBOR>) return Serializable::DeserializeBoundedCbor(data,size,value);
    else { static_assert(std::is_same_v<Format,Serializable::JSON>,"Unsupported State persistence format"); return Serializable::DeserializeBoundedJson(data,size,value); }
}
inline bool MakeKey(const System::DeviceIdentifier& device,StateTypeId type,Persistence::AtomicRecordKey& output) noexcept {
    std::array<char,28> raw{};
    raw[0]='S';raw[1]='T';raw[2]='V';raw[3]='1';
    for(std::size_t i=0;i<System::DeviceIdentifier::Size;++i) raw[4+i]=static_cast<char>(device.Bytes()[i]);
    auto value=type.Value();for(std::size_t i=0;i<8;++i){raw[20+i]=static_cast<char>(value);value>>=8;}
    return Persistence::AtomicRecordKey::TryCreate(std::string_view(raw.data(),raw.size()),output);
}
}

template<class TState>
struct StatePersistenceBindingView final {
    using Value=typename TState::ValueType;
    void* Owner=nullptr;
    bool (*Validate)(const void*) noexcept=nullptr;
    StateRuntimeStatus (*Restore)(void*,Value&,Timing::QualifiedTime&,bool&) noexcept=nullptr;
    bool (*Commit)(void*,const Value&,Timing::QualifiedTime) noexcept=nullptr;
    constexpr explicit operator bool() const noexcept { return Owner && Validate && Restore && Commit; }
};

/// <summary>Fixed State persistence binding over the crash-consistent P4 atomic-record seam.</summary>
/// <remarks>The record contains only authoritative Value, original TruthTime and compatibility facts.
/// Runtime incarnation, compact revision/phase, observer state and remote sessions are intentionally absent.
/// A successful ReplaceAtomically is the durable-before-RAM commit point used by StateOwner::Set.</remarks>
template<class TState,class Format=Serializable::DirectBinary>
class StatePersistenceBinding final {
    using Value=typename TState::ValueType;
    static_assert(Serializable::IsBoundedSerializable<Value>,"Persistent State requires a bounded P3 Value schema");
    static constexpr std::size_t PayloadMaximum=Serializable::MaximumSerializedSize<Value,Format>;
    static_assert(PayloadMaximum<=UINT32_MAX && PayloadMaximum<=SIZE_MAX-PersistenceDetail::HeaderBytes);
    Persistence::IAtomicRecordStore* _store=nullptr;
    mutable Persistence::AtomicRecordKey _key{};

    bool EnsureKey() const noexcept {
        if(_key) return true;
        System::DeviceRuntimeIdentity identity{};
        return System::RuntimeIdentity::TryRead(identity) && PersistenceDetail::MakeKey(identity.Device,TState::TypeId,_key);
    }
    bool ValidateImpl() const noexcept {
        if(!_store || !EnsureKey()) return false;
        const auto caps=_store->Capabilities();
        return caps.DurableOldOrNew && caps.BoundedOperations && caps.MaximumRecords>0 && caps.MaximumRecordBytes>=MaximumRecordBytes;
    }
    StateRuntimeStatus RestoreImpl(Value& value,Timing::QualifiedTime& truth,bool& hasValue) noexcept {
        hasValue=false;
        if(!ValidateImpl()) return StateRuntimeStatus::InvalidConfiguration;
        if(_store->Recover()!=Persistence::AtomicRecordStatus::Success) return StateRuntimeStatus::PersistenceFailure;
        std::array<std::uint8_t,MaximumRecordBytes> bytes{};
        std::size_t size=0;
        const auto read=_store->Read(_key,bytes.data(),bytes.size(),size);
        if(read==Persistence::AtomicRecordStatus::NotFound) return StateRuntimeStatus::Success;
        if(read!=Persistence::AtomicRecordStatus::Success || size<PersistenceDetail::HeaderBytes) return StateRuntimeStatus::PersistenceFailure;
        if(bytes[0]!='S'||bytes[1]!='T'||bytes[2]!='P'||bytes[3]!='1'||bytes[4]!=PersistenceDetail::RecordVersion||
           bytes[5]!=PersistenceDetail::FormatCode<Format>()||bytes[6]!=0||bytes[7]!=0) return StateRuntimeStatus::PersistenceFailure;
        System::DeviceRuntimeIdentity identity{};if(!System::RuntimeIdentity::TryRead(identity)) return StateRuntimeStatus::IdentityUnavailable;
        for(std::size_t i=0;i<System::DeviceIdentifier::Size;++i) if(bytes[8+i]!=identity.Device.Bytes()[i]) return StateRuntimeStatus::PersistenceFailure;
        if(PersistenceDetail::ReadLE(bytes.data()+24,8)!=TState::TypeId.Value()) return StateRuntimeStatus::PersistenceFailure;
        if(PersistenceDetail::ReadLE(bytes.data()+32,4)!=Serializable::SerializationTraits<Value>::CurrentVersion) return StateRuntimeStatus::PersistenceFailure;
        truth.Nanoseconds=PersistenceDetail::ReadLE(bytes.data()+36,8);
        truth.Reliability=static_cast<Timing::TimeReliability>(bytes[44]);
        if(!Timing::IsValidTimeReliability(truth.Reliability) || bytes[45]!=0 || bytes[46]!=0 || bytes[47]!=0) return StateRuntimeStatus::PersistenceFailure;
        const auto payload=static_cast<std::size_t>(PersistenceDetail::ReadLE(bytes.data()+48,4));
        if(payload>PayloadMaximum || size!=PersistenceDetail::HeaderBytes+payload) return StateRuntimeStatus::PersistenceFailure;
        Value candidate=value;
        const auto decoded=PersistenceDetail::Deserialize<Value,Format>(bytes.data()+PersistenceDetail::HeaderBytes,payload,candidate);
        if(!decoded || decoded.Bytes!=payload) return StateRuntimeStatus::PersistenceFailure;
        value=std::move(candidate);hasValue=true;return StateRuntimeStatus::Success;
    }
    bool CommitImpl(const Value& value,Timing::QualifiedTime truth) noexcept {
        if(!ValidateImpl() || !Timing::IsValidTimeReliability(truth.Reliability)) return false;
        std::array<std::uint8_t,MaximumRecordBytes> bytes{};
        const auto encoded=PersistenceDetail::Serialize<Value,Format>(value,bytes.data()+PersistenceDetail::HeaderBytes,PayloadMaximum);
        if(!encoded || encoded.Bytes>PayloadMaximum) return false;
        bytes[0]='S';bytes[1]='T';bytes[2]='P';bytes[3]='1';bytes[4]=PersistenceDetail::RecordVersion;bytes[5]=PersistenceDetail::FormatCode<Format>();
        bytes[6]=0;bytes[7]=0;
        System::DeviceRuntimeIdentity identity{};if(!System::RuntimeIdentity::TryRead(identity)) return false;
        for(std::size_t i=0;i<System::DeviceIdentifier::Size;++i) bytes[8+i]=identity.Device.Bytes()[i];
        PersistenceDetail::WriteLE(bytes.data()+24,TState::TypeId.Value(),8);
        PersistenceDetail::WriteLE(bytes.data()+32,Serializable::SerializationTraits<Value>::CurrentVersion,4);
        PersistenceDetail::WriteLE(bytes.data()+36,truth.Nanoseconds,8);
        bytes[44]=static_cast<std::uint8_t>(truth.Reliability);bytes[45]=bytes[46]=bytes[47]=0;
        PersistenceDetail::WriteLE(bytes.data()+48,encoded.Bytes,4);
        return _store->ReplaceAtomically(_key,bytes.data(),PersistenceDetail::HeaderBytes+encoded.Bytes)==Persistence::AtomicRecordStatus::Success;
    }
public:
    static constexpr std::size_t MaximumRecordBytes=PersistenceDetail::HeaderBytes+PayloadMaximum;
    explicit StatePersistenceBinding(Persistence::IAtomicRecordStore& store) noexcept:_store(&store){}
    StatePersistenceBindingView<TState> View() noexcept {
        return {this,
            [](const void* p) noexcept{return static_cast<const StatePersistenceBinding*>(p)->ValidateImpl();},
            [](void* p,Value& value,Timing::QualifiedTime& truth,bool& has) noexcept{return static_cast<StatePersistenceBinding*>(p)->RestoreImpl(value,truth,has);},
            [](void* p,const Value& value,Timing::QualifiedTime truth) noexcept{return static_cast<StatePersistenceBinding*>(p)->CommitImpl(value,truth);} };
    }
};

} // namespace ESPressio::State
