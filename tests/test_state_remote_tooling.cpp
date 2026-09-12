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

struct RemoteValue final {
    std::uint32_t Number=0;
    constexpr bool operator==(const RemoteValue& other) const noexcept { return Number==other.Number; }
    ESPRESSIO_SERIALIZABLE_TYPE(RemoteValue)
    ESPRESSIO_SERIALIZABLE_SCHEMA_VERSION(1)
    ESPRESSIO_SERIALIZABLE_PROPERTIES(ESPRESSIO_PROPERTY("number",Number))
};
struct Policy final {
    using PolicyCategory=Primitive::StateConvergencePolicyTag;
    using RequiredEvidence=Primitive::DestinationPrimitiveAdmission;
    using Supersession=Primitive::LatestAuthoritativeValue;
    using ExhaustionDisposition=Primitive::DormantNeedsConvergence;
    static constexpr std::uint64_t MaximumResidenceNanoseconds=1000000000ULL;
    static constexpr std::uint64_t MaximumAdapterAdmissionWaitNanoseconds=1000000ULL;
    static constexpr std::uint16_t MaximumAttempts=3;
    static constexpr std::uint64_t MinimumRetrySpacingNanoseconds=1000ULL;
    static constexpr std::uint64_t MaximumRetrySpacingNanoseconds=1000000ULL;
};
struct RemoteState final:S::TransmissibleState<RemoteState,RemoteValue>{
    static constexpr S::StateTypeId TypeId{0x5701};
    static constexpr std::string_view CanonicalName="Test.State.RemoteTooling";
    using ConvergencePolicy=Policy;
};
struct LocalState final:S::SerializableState<LocalState,RemoteValue>{
    static constexpr S::StateTypeId TypeId{0x5702};
    static constexpr std::string_view CanonicalName="Test.State.LocalTooling";
};

static System::DeviceRuntimeIdentity Identity(std::uint8_t marker){
    System::DeviceIdentifier::Storage bytes{};bytes[0]=marker;
    return {System::DeviceIdentifier{bytes},System::RuntimeIncarnationId{1}};
}
static Timing::QualifiedTime Capture(){return {1234,Timing::TimeReliability::Holdover};}

int main(){
    const auto local=Identity(1),remote=Identity(2);
    assert(System::RuntimeIdentity::Install(local)==System::RuntimeIdentity::InstallationStatus::Success);

    Primitive::TypeDirectory<2> directory;
    assert(directory.Register<RemoteState>()==Primitive::TypeDirectoryRegistrationStatus::Success);
    assert(directory.Register<LocalState>()==Primitive::TypeDirectoryRegistrationStatus::Success);
    assert(directory.Initialize()==Primitive::TypeDirectoryInitializationStatus::Success);

    std::array<S::StateTypeIntrospectionEntry,2> entries{};
    auto enumerated=S::EnumerateStateTypes(directory.View(),entries.data(),entries.size());
    assert(enumerated && enumerated.Count==2 && enumerated.Required==2);
    assert(entries[0] && entries[1]);
    assert(entries[0].Common->Key.Family==S::StateFamilyId && entries[1].Common->Key.Family==S::StateFamilyId);
    assert(S::FindStateType(directory.View(),RemoteState::TypeId));
    assert(!S::FindStateType(directory.View(),S::StateTypeId{9999}));
    std::array<S::StateTypeIntrospectionEntry,1> shortEntries{};
    auto shortEnumeration=S::EnumerateStateTypes(directory.View(),shortEntries.data(),shortEntries.size());
    assert(shortEnumeration.Status==S::StateTypeEnumerationStatus::InsufficientOutput);
    assert(shortEnumeration.Count==1 && shortEnumeration.Required==2);

    using RemoteConfig=S::TypeConfiguration<RemoteState,S::MaximumRemoteOwners<1>,S::MaximumSubscribers<0>>;
    using LocalConfig=S::TypeConfiguration<LocalState>;
    S::Runtime<RemoteConfig,LocalConfig> runtime;
    auto remoteOwner=runtime.BindOwner<RemoteState>();assert(remoteOwner);
    auto localOwner=runtime.BindOwner<LocalState>();assert(localOwner);
    assert(runtime.Initialize(directory.View(),&Capture)==S::StateRuntimeStatus::Success);
    assert(runtime.Start()==S::StateRuntimeStatus::Success);

    std::array<std::uint8_t,Serializable::MaximumSerializedSize<RemoteValue,Serializable::DirectBinary>> bytes{};
    auto noValue=S::ReadDynamicRemoteState(runtime,RemoteState::TypeId,remote.Device,
                                           S::StatePayloadFormat::DirectBinary,bytes.data(),bytes.size());
    assert(noValue.Status==S::StateDynamicRemoteReadStatus::NoValue);
    auto notRemote=S::ReadDynamicRemoteState(runtime,LocalState::TypeId,remote.Device,
                                             S::StatePayloadFormat::DirectBinary,bytes.data(),bytes.size());
    assert(notRemote.Status==S::StateDynamicRemoteReadStatus::NotTransmissible);
    auto unknown=S::ReadDynamicRemoteState(runtime,S::StateTypeId{0xFFFF},remote.Device,
                                           S::StatePayloadFormat::DirectBinary,bytes.data(),bytes.size());
    assert(unknown.Status==S::StateDynamicRemoteReadStatus::UnknownType);

    const auto session=runtime.ReserveSubscriptionSession<RemoteState>(remote.Device);assert(session);
    S::StateSnapshot<RemoteState> snapshot{{42},Capture()};
    assert(runtime.InstallSubscribeSnapshot<RemoteState>(remote,session.Handle.Session,{false,1},snapshot)==S::StateRemoteStatus::Success);

    auto read=S::ReadDynamicRemoteState(runtime,RemoteState::TypeId,remote.Device,
                                        S::StatePayloadFormat::DirectBinary,bytes.data(),bytes.size());
    assert(read && read.Bytes>0 && read.TruthTime.Nanoseconds==1234);
    assert(read.Session==S::StateRemoteSessionState::ActiveTrusted);
    RemoteValue decoded{};
    assert(Serializable::DeserializeBoundedDirectBinary(bytes.data(),read.Bytes,decoded));
    assert(decoded.Number==42);

    auto insufficient=S::ReadDynamicRemoteState(runtime,RemoteState::TypeId,remote.Device,
                                                 S::StatePayloadFormat::DirectBinary,bytes.data(),0);
    assert(insufficient.Status==S::StateDynamicRemoteReadStatus::InsufficientOutput);
    assert(insufficient.TruthTime.Nanoseconds==1234);

    // Retained last-known truth remains dynamically readable after session closure;
    // session state is exposed separately from the immutable Value+TruthTime payload.
    assert(runtime.Unsubscribe<RemoteState>(remote.Device,S::StateReplicaRelease::RetainLastKnown)==S::StateRemoteStatus::Success);
    auto retained=S::ReadDynamicRemoteState(runtime,RemoteState::TypeId,remote.Device,
                                            S::StatePayloadFormat::DirectBinary,bytes.data(),bytes.size());
    assert(retained && retained.Session==S::StateRemoteSessionState::Inactive);
    decoded={};
    assert(Serializable::DeserializeBoundedDirectBinary(bytes.data(),retained.Bytes,decoded));
    assert(decoded.Number==42);

    assert(runtime.Shutdown()==S::StateRuntimeStatus::Success);
    auto afterShutdown=S::ReadDynamicRemoteState(runtime,RemoteState::TypeId,remote.Device,
                                                 S::StatePayloadFormat::DirectBinary,bytes.data(),bytes.size());
    assert(afterShutdown && afterShutdown.Session==S::StateRemoteSessionState::Inactive);
}
