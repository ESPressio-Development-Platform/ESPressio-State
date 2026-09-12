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

struct RuntimeValue final {
    std::uint32_t Value=0;
    constexpr bool operator==(const RuntimeValue& other) const noexcept { return Value==other.Value; }
    ESPRESSIO_SERIALIZABLE_TYPE(RuntimeValue)
    ESPRESSIO_SERIALIZABLE_SCHEMA_VERSION(1)
    ESPRESSIO_SERIALIZABLE_PROPERTIES(ESPRESSIO_PROPERTY("value",Value))
};
struct RuntimePolicy final {
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
struct RuntimeState final : S::TransmissibleState<RuntimeState,RuntimeValue> {
    static constexpr S::StateTypeId TypeId{0x5501};
    static constexpr std::string_view CanonicalName="Test.State.RemoteRuntime";
    using ConvergencePolicy=RuntimePolicy;
};

static System::DeviceRuntimeIdentity Identity(std::uint8_t marker,std::uint32_t runtime){
    System::DeviceIdentifier::Storage bytes{};bytes[0]=marker;
    return {System::DeviceIdentifier{bytes},System::RuntimeIncarnationId{runtime}};
}
static Timing::QualifiedTime Capture(){return {88,Timing::TimeReliability::Synchronized};}

struct Adapter final {
    S::StateOutboundMessage<RuntimeState> Last{};
    unsigned Admissions=0;
    bool Accept=true;
    bool Validate(const S::StateTransportContract&) noexcept { return true; }
    S::StateTransportAdmission Admit(const S::StateOutboundMessage<RuntimeState>& message) noexcept {
        Last=message;++Admissions;return {Accept?S::StateTransportAdmissionStatus::Accepted
                                                :S::StateTransportAdmissionStatus::CapacityUnavailable};
    }
};

int main(){
    using Format=Serializable::DirectBinary;
    const auto local=Identity(1,1),remoteOwner=Identity(2,2),remoteRequester=Identity(3,3);
    assert(System::RuntimeIdentity::Install(local)==System::RuntimeIdentity::InstallationStatus::Success);
    Primitive::TypeDirectory<1> directory;
    assert(directory.Register<RuntimeState>()==Primitive::TypeDirectoryRegistrationStatus::Success);
    assert(directory.Initialize()==Primitive::TypeDirectoryInitializationStatus::Success);
    Adapter adapter;
    S::StateTransportBinding<RuntimeState,Format> binding;
    assert((binding.Initialize<Adapter,&Adapter::Admit,&Adapter::Validate>(adapter)));
    using Runtime=S::Runtime<S::TypeConfiguration<RuntimeState,S::MaximumRemoteOwners<2>,S::MaximumSubscribers<2>>>;
    Runtime runtime;
    auto owner=runtime.BindOwner<RuntimeState>();assert(owner);
    assert(runtime.BindTransport(binding)==S::StateRuntimeStatus::Success);
    assert(runtime.Initialize(directory.View(),&Capture)==S::StateRuntimeStatus::Success);
    assert(runtime.Start()==S::StateRuntimeStatus::Success);
    // A first fact committed between SubscribeNoValue and SubscribeAccepted must
    // remain eligible for the first protected baseline once establishment finishes.
    S::StateRemoteReplicaTable<RuntimeState,0,1> emptyHandshake;
    assert(emptyHandshake.ReserveSubscriber(remoteRequester,S::StateSessionToken{90})==S::StateRemoteStatus::Success);
    assert(emptyHandshake.OfferSubscriberBaseline(remoteRequester,S::StateSessionToken{90},false)==S::StateRemoteStatus::Success);
    emptyHandshake.MarkLatestDirty({false,1});
    assert(emptyHandshake.AcceptSubscriberEstablishment(remoteRequester,S::StateSessionToken{90},{false,1})==S::StateRemoteStatus::Success);
    assert(emptyHandshake.SubscriberDirty(remoteRequester.Device));
    S::StateSourceWork firstBaseline{};
    assert(emptyHandshake.TryPrepareLatest({false,1},firstBaseline));
    assert(firstBaseline.Kind==S::StateMessageKind::BaselineSnapshot);
    assert(emptyHandshake.BeginSubscriberResync(remoteRequester,S::StateSessionToken{90},S::StateResyncToken{12},{false,1})==S::StateRemoteStatus::Success);
    // An ordinary ACK has no resync token and cannot complete even the same-version resync.
    assert(emptyHandshake.AcceptSubscriberBaseline(remoteRequester,S::StateSessionToken{90},{false,1})==S::StateRemoteStatus::SessionMismatch);
    assert(emptyHandshake.SubscriberState(remoteRequester.Device)==S::StateRemoteSessionState::AwaitingResync);

    assert(owner.Set({10},{100,Timing::TimeReliability::Synchronized})==S::StateSetStatus::Changed);

    // Source side: a validated SubscribeRequest reserves bounded state and captures one atomic baseline.
    S::StateControlWireHeader subscribe{S::StateMessageKind::SubscribeRequest,RuntimeState::TypeId,
        local,remoteRequester,S::StateSessionToken{41},{},0};
    std::array<std::uint8_t,S::MaximumCompleteStateSnapshotControlWireBytes<RuntimeState,Format>> bytes{};
    auto encoded=S::EncodeStateControl(subscribe,bytes.data(),bytes.size());assert(encoded);
    auto admitted=runtime.AdmitRemote<RuntimeState,Format>(bytes.data(),encoded.Bytes,{remoteRequester});
    assert(admitted && adapter.Last.Kind==S::StateMessageKind::SubscribeSnapshot);
    assert(adapter.Last.Snapshot.Value.Value==10 && (adapter.Last.Version==S::StateVersion{false,1}));
    assert(runtime.GetSourceSubscriberStatus<RuntimeState>(remoteRequester.Device)==S::StateRemoteSessionState::Establishing);

    // The canonical truth may advance while the subscriber is still establishing.
    assert(owner.Set({11},{110,Timing::TimeReliability::Synchronized})==S::StateSetStatus::Changed);

    S::StateControlWireHeader subscribeAccepted{S::StateMessageKind::SubscribeAccepted,RuntimeState::TypeId,
        local,remoteRequester,S::StateSessionToken{41},{},0};
    encoded=S::EncodeStateControl(subscribeAccepted,bytes.data(),bytes.size());assert(encoded);
    admitted=runtime.AdmitRemote<RuntimeState,Format>(bytes.data(),encoded.Bytes,{remoteRequester});
    assert(admitted);
    assert(runtime.GetSourceSubscriberStatus<RuntimeState>(remoteRequester.Device)==S::StateRemoteSessionState::ActiveTrusted);
    assert(runtime.SourceSubscriberDirty<RuntimeState>(remoteRequester.Device));
    assert(runtime.ServiceLatest<RuntimeState>());
    assert(adapter.Last.Kind==S::StateMessageKind::Publication && adapter.Last.Snapshot.Value.Value==11);
    assert((adapter.Last.Version==S::StateVersion{false,2}));
    assert(!runtime.SourceSubscriberDirty<RuntimeState>(remoteRequester.Device));

    // The source accepts evidence only for the exact version offered to this session.
    S::StateAcceptanceControlWireHeader publicationAccepted{{S::StateMessageKind::PublicationAccepted,
        RuntimeState::TypeId,local,remoteRequester,S::StateSessionToken{41},{},0},{false,2}};
    encoded=S::EncodeStateAcceptanceControl(publicationAccepted,bytes.data(),bytes.size());assert(encoded);
    admitted=runtime.AdmitRemote<RuntimeState,Format>(bytes.data(),encoded.Bytes,{remoteRequester});assert(admitted);
    assert((runtime.SourceAcceptedBaseline<RuntimeState>(remoteRequester.Device)==S::StateVersion{false,2}));

    // A failed adapter admission retains the current latest truth for a later service opportunity.
    assert(owner.Set({12},{120,Timing::TimeReliability::Synchronized})==S::StateSetStatus::Changed);
    adapter.Accept=false;
    assert(!runtime.ServiceLatest<RuntimeState>());
    assert(runtime.SourceSubscriberDirty<RuntimeState>(remoteRequester.Device));
    adapter.Accept=true;
    assert(runtime.ServiceLatest<RuntimeState>());
    assert(adapter.Last.Snapshot.Value.Value==12 && (adapter.Last.Version==S::StateVersion{false,3}));

    // Requester side: source snapshot is committed before SubscribeAccepted is offered.
    const auto session=runtime.SubscribeFrom<RuntimeState>(remoteOwner.Device);assert(session);
    const S::StateSnapshot<RuntimeState> remoteInitial{{20},{200,Timing::TimeReliability::Synchronized}};
    S::StateControlWireHeader snapshotControl{S::StateMessageKind::SubscribeSnapshot,RuntimeState::TypeId,
        remoteOwner,local,session.Handle.Session,{},0};
    encoded=S::EncodeStateSnapshotControl<RuntimeState,Format>(snapshotControl,remoteInitial,{false,7},bytes.data(),bytes.size());assert(encoded);
    admitted=runtime.AdmitRemote<RuntimeState,Format>(bytes.data(),encoded.Bytes,{remoteOwner});
    assert(admitted && adapter.Last.Kind==S::StateMessageKind::SubscribeAccepted);
    S::StateSnapshot<RuntimeState> read{};
    assert(runtime.TryReadRemote<RuntimeState>(remoteOwner.Device,read) && read.Value.Value==20);
    assert(runtime.GetRemoteSessionStatus<RuntimeState>(remoteOwner.Device)==S::StateRemoteSessionState::ActiveTrusted);

    // A lost acceptance can cause an exact snapshot retry; it is re-acknowledged without mutation.
    admitted=runtime.AdmitRemote<RuntimeState,Format>(bytes.data(),encoded.Bytes,{remoteOwner});
    assert(admitted.Disposition==Primitive::PrimitiveAdmissionDisposition::AlreadyAccepted);
    assert(adapter.Last.Kind==S::StateMessageKind::SubscribeAccepted);

    const S::StateSnapshot<RuntimeState> remoteNext{{21},{210,Timing::TimeReliability::Synchronized}};
    std::array<std::uint8_t,S::MaximumCompleteStatePublicationWireBytes<RuntimeState,Format>> publication{};
    encoded=S::EncodeStatePublication<RuntimeState,Format>(remoteNext,{false,8},remoteOwner,local,session.Handle.Session,
                                                           publication.data(),publication.size());assert(encoded);
    admitted=runtime.AdmitRemote<RuntimeState,Format>(publication.data(),encoded.Bytes,{remoteOwner});
    assert(admitted && adapter.Last.Kind==S::StateMessageKind::PublicationAccepted);
    assert(runtime.TryReadRemote<RuntimeState>(remoteOwner.Device,read) && read.Value.Value==21);

    // ResyncRequired allocates a fresh requester token and a matching snapshot commits/re-acks idempotently.
    S::StateControlWireHeader required{S::StateMessageKind::ResyncRequired,RuntimeState::TypeId,
        remoteOwner,local,session.Handle.Session,{},0};
    // Valid provenance alone is insufficient: delayed controls from an old session or
    // incarnation must not invalidate the replacement session or emit a resync request.
    const auto admissionsBeforeStale=adapter.Admissions;
    auto staleRequired=required;
    staleRequired.Session=S::StateSessionToken{session.Handle.Session.Value()+1};
    encoded=S::EncodeStateControl(staleRequired,bytes.data(),bytes.size());assert(encoded);
    admitted=runtime.AdmitRemote<RuntimeState,Format>(bytes.data(),encoded.Bytes,{remoteOwner});
    assert(admitted.Disposition==Primitive::PrimitiveAdmissionDisposition::Rejected);
    staleRequired=required;
    staleRequired.Owner.Incarnation=System::RuntimeIncarnationId{1};
    encoded=S::EncodeStateControl(staleRequired,bytes.data(),bytes.size());assert(encoded);
    admitted=runtime.AdmitRemote<RuntimeState,Format>(bytes.data(),encoded.Bytes,{staleRequired.Owner});
    assert(admitted.Disposition==Primitive::PrimitiveAdmissionDisposition::Rejected);
    assert(adapter.Admissions==admissionsBeforeStale);
    assert(runtime.GetRemoteSessionStatus<RuntimeState>(remoteOwner.Device)==S::StateRemoteSessionState::ActiveTrusted);
    assert(runtime.TryReadRemote<RuntimeState>(remoteOwner.Device,read) && read.Value.Value==21);
    encoded=S::EncodeStateControl(required,bytes.data(),bytes.size());assert(encoded);
    admitted=runtime.AdmitRemote<RuntimeState,Format>(bytes.data(),encoded.Bytes,{remoteOwner});assert(admitted);
    assert(adapter.Last.Kind==S::StateMessageKind::ResyncRequest && adapter.Last.Resync);
    const auto resync=adapter.Last.Resync;
    const S::StateSnapshot<RuntimeState> resynced{{30},{300,Timing::TimeReliability::Synchronized}};
    S::StateControlWireHeader resyncControl{S::StateMessageKind::ResyncSnapshot,RuntimeState::TypeId,
        remoteOwner,local,session.Handle.Session,resync,0};
    encoded=S::EncodeStateSnapshotControl<RuntimeState,Format>(resyncControl,resynced,{false,30},bytes.data(),bytes.size());assert(encoded);
    admitted=runtime.AdmitRemote<RuntimeState,Format>(bytes.data(),encoded.Bytes,{remoteOwner});assert(admitted);
    assert(adapter.Last.Kind==S::StateMessageKind::ResyncAccepted && adapter.Last.Resync==resync);
    admitted=runtime.AdmitRemote<RuntimeState,Format>(bytes.data(),encoded.Bytes,{remoteOwner});
    assert(admitted.Disposition==Primitive::PrimitiveAdmissionDisposition::AlreadyAccepted);
    assert(adapter.Last.Kind==S::StateMessageKind::ResyncAccepted);
    assert(runtime.TryReadRemote<RuntimeState>(remoteOwner.Device,read) && read.Value.Value==30);

    assert(runtime.Unsubscribe<RuntimeState>(session.Handle,S::StateReplicaRelease::RetainLastKnown)==S::StateRemoteStatus::Success);
    const auto admissionsAfterClose=adapter.Admissions;
    encoded=S::EncodeStateControl(required,bytes.data(),bytes.size());assert(encoded);
    admitted=runtime.AdmitRemote<RuntimeState,Format>(bytes.data(),encoded.Bytes,{remoteOwner});
    assert(admitted.Disposition==Primitive::PrimitiveAdmissionDisposition::Rejected);
    assert(adapter.Admissions==admissionsAfterClose);
    assert(runtime.GetRemoteSessionStatus<RuntimeState>(remoteOwner.Device)==S::StateRemoteSessionState::Inactive);
    assert(runtime.TryReadRemote<RuntimeState>(remoteOwner.Device,read) && read.Value.Value==30);

    assert(runtime.Shutdown()==S::StateRuntimeStatus::Success);
}
