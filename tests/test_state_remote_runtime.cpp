#include <ESPressio_States.hpp>
#include <ESPressio_RuntimeIdentity.hpp>
#include <ESPressio_SerializationMacros.hpp>
#include <ESPressio_TypeDirectory.hpp>
#include <array>
#include <cassert>
#include <cstdint>
#include <string_view>
#include <memory>
#include <mutex>
#include <shared_mutex>

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

// Instrument the provider boundary: family ingress may only attempt locks,
// including when a selected attempt fails. Initialization may resolve storage.
struct AdmissionMutex final : System::Synchronization::IMutex {
    inline static bool NonBlockingOnly=false;
    inline static unsigned Attempts=0,RejectAttempt=0;
    std::mutex Mutex;
    void Lock() noexcept override { assert(!NonBlockingOnly);Mutex.lock(); }
    bool TryLock() noexcept override {
        if(NonBlockingOnly && ++Attempts==RejectAttempt) return false;
        return Mutex.try_lock();
    }
    void Unlock() noexcept override { Mutex.unlock(); }
};
struct AdmissionReadWrite final : System::Synchronization::IReadWriteLock {
    inline static bool RejectShared=false;
    std::shared_mutex Mutex;
    void Lock() noexcept override { assert(!AdmissionMutex::NonBlockingOnly);Mutex.lock(); }
    bool TryLock() noexcept override { return Mutex.try_lock(); }
    void Unlock() noexcept override { Mutex.unlock(); }
    void LockShared() noexcept override { assert(!AdmissionMutex::NonBlockingOnly);Mutex.lock_shared(); }
    bool TryLockShared() noexcept override { return !RejectShared && Mutex.try_lock_shared(); }
    void UnlockShared() noexcept override { Mutex.unlock_shared(); }
};
struct AdmissionSynchronization final : System::Synchronization::ISynchronizationProvider {
    std::unique_ptr<System::Synchronization::ISignal> CreateBinarySignal(bool) override { return {}; }
    std::unique_ptr<System::Synchronization::IReadWriteLock> CreateReadWriteLock() override {
        assert(!AdmissionMutex::NonBlockingOnly);
        return std::make_unique<AdmissionReadWrite>();
    }
    std::unique_ptr<System::Synchronization::IMutex> CreateMutex() override {
        assert(!AdmissionMutex::NonBlockingOnly);
        return std::make_unique<AdmissionMutex>();
    }
};

int main(){
    static AdmissionSynchronization synchronization;
    System::Synchronization::SetProvider(&synchronization);
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
    auto admit=[&](const std::uint8_t* data,std::size_t size,S::StateValidatedIngressContext ingress) {
        AdmissionMutex::Attempts=0;AdmissionMutex::NonBlockingOnly=true;
        const auto result=runtime.AdmitRemote<RuntimeState,Format>(data,size,ingress);
        AdmissionMutex::NonBlockingOnly=false;
        return result;
    };
    // A first fact committed between SubscribeNoValue and SubscribeAccepted must
    // remain eligible for the first protected baseline once establishment finishes.
    S::StateRemoteReplicaTable<RuntimeState,0,1> emptyHandshake;
    assert(emptyHandshake.ReserveSubscriber(remoteRequester,S::StateSessionToken{90})==S::StateRemoteStatus::Success);
    assert(emptyHandshake.OfferSubscriberBaseline(remoteRequester,S::StateSessionToken{90},false)==S::StateRemoteStatus::Success);
    emptyHandshake.MarkLatestDirty({false,1});
    assert(emptyHandshake.AcceptSubscriberEstablishment(remoteRequester,S::StateSessionToken{90},{false,1})==S::StateRemoteStatus::Success);
    assert(emptyHandshake.SubscriberDirty(remoteRequester.Device));
    S::StateSnapshot<RuntimeState> firstSnapshot{{1},{1,Timing::TimeReliability::Synchronized}};
    S::StateSourceWork firstBaseline{};
    assert(emptyHandshake.TryPrepareLatest({false,1},firstSnapshot,firstBaseline));
    assert(firstBaseline.Kind==S::StateMessageKind::BaselineSnapshot);
    assert(emptyHandshake.BeginSubscriberResync(remoteRequester,S::StateSessionToken{90},S::StateResyncToken{12},{false,1})==S::StateRemoteStatus::Success);
    // An ordinary ACK has no resync token and cannot complete even the same-version resync.
    assert(emptyHandshake.AcceptSubscriberBaseline(remoteRequester,S::StateSessionToken{90},{false,1})==S::StateRemoteStatus::SessionMismatch);
    assert(emptyHandshake.SubscriberState(remoteRequester.Device)==S::StateRemoteSessionState::AwaitingResync);
    emptyHandshake.MarkLatestDirty({false,2});
    assert(emptyHandshake.AcceptSubscriberResync(remoteRequester,S::StateSessionToken{90},S::StateResyncToken{12},{false,1},{false,1})==S::StateRemoteStatus::Success);
    assert(emptyHandshake.SubscriberDirty(remoteRequester.Device));

    // Model the acceptance's canonical read racing a later Set before its table
    // transaction. The table's committed latest version must win over that read.
    S::StateRemoteReplicaTable<RuntimeState,0,1> racingHandshake;
    assert(racingHandshake.ReserveSubscriber(remoteRequester,S::StateSessionToken{91})==S::StateRemoteStatus::Success);
    bool handshakeHasValue=true;
    S::StateVersion handshakeVersion{false,1};
    S::StateSnapshot<RuntimeState> handshakeSnapshot{{1},{1,Timing::TimeReliability::Synchronized}};
    assert(racingHandshake.PrepareSubscriberEstablishment(remoteRequester,S::StateSessionToken{91},handshakeHasValue,handshakeVersion,handshakeSnapshot)==S::StateRemoteStatus::Success);
    racingHandshake.MarkLatestDirty({false,2});
    assert(racingHandshake.AcceptSubscriberEstablishment(remoteRequester,S::StateSessionToken{91},{false,1})==S::StateRemoteStatus::Success);
    assert(racingHandshake.SubscriberDirty(remoteRequester.Device));
    S::StateSourceWork racingWork{};
    assert(!racingHandshake.TryPrepareLatest({false,1},handshakeSnapshot,racingWork));
    assert(racingHandshake.SubscriberDirty(remoteRequester.Device));
    assert(racingHandshake.TryPrepareLatest({false,2},handshakeSnapshot,racingWork));
    racingHandshake.MarkLatestDirty({false,3});
    racingHandshake.CompleteLatestTransfer(racingWork,true);
    assert(racingHandshake.SubscriberDirty(remoteRequester.Device));
    assert(racingHandshake.ReserveSubscriber(remoteRequester,S::StateSessionToken{90})==S::StateRemoteStatus::SessionMismatch);
    assert(racingHandshake.SubscriberState(remoteRequester.Device)==S::StateRemoteSessionState::ActiveTrusted);
    assert(racingHandshake.ReserveSubscriber(remoteRequester,S::StateSessionToken{92})==S::StateRemoteStatus::Success);
    handshakeHasValue=false;handshakeVersion={};
    assert(racingHandshake.PrepareSubscriberEstablishment(remoteRequester,S::StateSessionToken{92},handshakeHasValue,handshakeVersion,handshakeSnapshot)==S::StateRemoteStatus::Success);
    racingHandshake.MarkLatestDirty({false,4});
    handshakeHasValue=true;handshakeVersion={false,4};
    assert(racingHandshake.PrepareSubscriberEstablishment(remoteRequester,S::StateSessionToken{92},handshakeHasValue,handshakeVersion,handshakeSnapshot)==S::StateRemoteStatus::Success);
    assert(!handshakeHasValue && !handshakeVersion);
    assert(racingHandshake.AcceptSubscriberEstablishment(remoteRequester,S::StateSessionToken{92},{})==S::StateRemoteStatus::Success);
    assert(racingHandshake.SubscriberDirty(remoteRequester.Device));

    // The first baseline following NoValue remains immutable until its ACK.
    handshakeSnapshot={{4},{4,Timing::TimeReliability::Synchronized}};
    assert(racingHandshake.TryPrepareLatest({false,4},handshakeSnapshot,racingWork));
    assert(racingWork.Kind==S::StateMessageKind::BaselineSnapshot);
    racingHandshake.CompleteLatestTransfer(racingWork,true);
    racingHandshake.MarkLatestDirty({false,5});
    handshakeSnapshot={{5},{5,Timing::TimeReliability::Synchronized}};
    assert(racingHandshake.TryPrepareLatest({false,5},handshakeSnapshot,racingWork));
    assert((racingWork.Version==S::StateVersion{false,4}));
    assert(handshakeSnapshot.Value.Value==4 && handshakeSnapshot.TruthTime.Nanoseconds==4);
    assert(racingHandshake.AcceptSubscriberBaseline(remoteRequester,S::StateSessionToken{92},{false,4})==S::StateRemoteStatus::Success);
    assert(racingHandshake.SubscriberDirty(remoteRequester.Device));
    handshakeSnapshot={{5},{5,Timing::TimeReliability::Synchronized}};
    assert(racingHandshake.TryPrepareLatest({false,5},handshakeSnapshot,racingWork));
    assert(racingWork.Kind==S::StateMessageKind::Publication);
    assert((racingWork.Version==S::StateVersion{false,5}) && handshakeSnapshot.Value.Value==5);

    // Loss of continuity creates a protected control opportunity. Admission
    // rejection retains it; admission transfers it once, and new truth rearms it.
    assert(racingHandshake.RequireSubscriberResync(remoteRequester.Device)==S::StateRemoteStatus::Success);
    assert(racingHandshake.TryPrepareLatest({false,5},handshakeSnapshot,racingWork));
    assert(racingWork.Kind==S::StateMessageKind::ResyncRequired);
    racingHandshake.CompleteLatestTransfer(racingWork,false);
    assert(racingHandshake.TryPrepareLatest({false,5},handshakeSnapshot,racingWork));
    racingHandshake.CompleteLatestTransfer(racingWork,true);
    assert(!racingHandshake.TryPrepareLatest({false,5},handshakeSnapshot,racingWork));
    racingHandshake.MarkLatestDirty({false,6});
    assert(racingHandshake.TryPrepareLatest({false,6},handshakeSnapshot,racingWork));
    racingHandshake.MarkLatestDirty({false,7});
    racingHandshake.CompleteLatestTransfer(racingWork,true);
    assert(racingHandshake.TryPrepareLatest({false,7},handshakeSnapshot,racingWork));
    assert(racingWork.Kind==S::StateMessageKind::ResyncRequired);

    assert(owner.Set({10},{100,Timing::TimeReliability::Synchronized})==S::StateSetStatus::Changed);

    // Source side: a validated SubscribeRequest reserves bounded state and captures one atomic baseline.
    S::StateControlWireHeader subscribe{S::StateMessageKind::SubscribeRequest,RuntimeState::TypeId,
        local,remoteRequester,S::StateSessionToken{41},{},0};
    std::array<std::uint8_t,S::MaximumCompleteStateSnapshotControlWireBytes<RuntimeState,Format>> bytes{};
    auto encoded=S::EncodeStateControl(subscribe,bytes.data(),bytes.size());assert(encoded);
    AdmissionReadWrite::RejectShared=true;
    assert(admit(bytes.data(),encoded.Bytes,{remoteRequester}).Disposition==Primitive::PrimitiveAdmissionDisposition::TemporarilyUnavailable);
    AdmissionReadWrite::RejectShared=false;
    for(unsigned attempt=1;attempt<=4;++attempt) {
        AdmissionMutex::RejectAttempt=attempt;
        const auto blocked=admit(bytes.data(),encoded.Bytes,{remoteRequester});
        assert(blocked.Disposition==Primitive::PrimitiveAdmissionDisposition::TemporarilyUnavailable);
        assert(adapter.Admissions==0);
    }
    AdmissionMutex::RejectAttempt=0;
    auto admitted=admit(bytes.data(),encoded.Bytes,{remoteRequester});
    assert(admitted && adapter.Last.Kind==S::StateMessageKind::SubscribeSnapshot);
    assert(adapter.Last.Snapshot.Value.Value==10 && (adapter.Last.Version==S::StateVersion{false,1}));
    assert(runtime.GetSourceSubscriberStatus<RuntimeState>(remoteRequester.Device)==S::StateRemoteSessionState::Establishing);

    // The canonical truth may advance while the subscriber is still establishing.
    assert(owner.Set({11},{110,Timing::TimeReliability::Synchronized})==S::StateSetStatus::Changed);

    // A retry of the same request must preserve the baseline identified by its
    // versionless SubscribeAccepted, even after the canonical truth advances.
    admitted=admit(bytes.data(),encoded.Bytes,{remoteRequester});
    assert(admitted && adapter.Last.Kind==S::StateMessageKind::SubscribeSnapshot);
    assert(adapter.Last.Snapshot.Value.Value==10 && (adapter.Last.Version==S::StateVersion{false,1}));

    S::StateControlWireHeader subscribeAccepted{S::StateMessageKind::SubscribeAccepted,RuntimeState::TypeId,
        local,remoteRequester,S::StateSessionToken{41},{},0};
    encoded=S::EncodeStateControl(subscribeAccepted,bytes.data(),bytes.size());assert(encoded);
    admitted=admit(bytes.data(),encoded.Bytes,{remoteRequester});
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
    admitted=admit(bytes.data(),encoded.Bytes,{remoteRequester});assert(admitted);
    assert((runtime.SourceAcceptedBaseline<RuntimeState>(remoteRequester.Device)==S::StateVersion{false,2}));

    // A failed adapter admission retains the current latest truth for a later service opportunity.
    assert(owner.Set({12},{120,Timing::TimeReliability::Synchronized})==S::StateSetStatus::Changed);
    adapter.Accept=false;
    assert(!runtime.ServiceLatest<RuntimeState>());
    assert(runtime.SourceSubscriberDirty<RuntimeState>(remoteRequester.Device));
    adapter.Accept=true;
    assert(runtime.ServiceLatest<RuntimeState>());
    assert(adapter.Last.Snapshot.Value.Value==12 && (adapter.Last.Version==S::StateVersion{false,3}));

    // Resync retransmissions also retain their token's exact snapshot. A later
    // canonical commit becomes follow-up work after the matching acceptance.
    S::StateControlWireHeader sourceResync{S::StateMessageKind::ResyncRequest,RuntimeState::TypeId,
        local,remoteRequester,S::StateSessionToken{41},S::StateResyncToken{93},0};
    encoded=S::EncodeStateControl(sourceResync,bytes.data(),bytes.size());assert(encoded);
    admitted=admit(bytes.data(),encoded.Bytes,{remoteRequester});assert(admitted);
    assert(adapter.Last.Kind==S::StateMessageKind::ResyncSnapshot && adapter.Last.Snapshot.Value.Value==12);
    assert(owner.Set({13},{130,Timing::TimeReliability::Synchronized})==S::StateSetStatus::Changed);
    admitted=admit(bytes.data(),encoded.Bytes,{remoteRequester});assert(admitted);
    assert(adapter.Last.Snapshot.Value.Value==12 && (adapter.Last.Version==S::StateVersion{false,3}));
    S::StateAcceptanceControlWireHeader sourceResyncAccepted{{S::StateMessageKind::ResyncAccepted,
        RuntimeState::TypeId,local,remoteRequester,S::StateSessionToken{41},S::StateResyncToken{93},0},{false,3}};
    encoded=S::EncodeStateAcceptanceControl(sourceResyncAccepted,bytes.data(),bytes.size());assert(encoded);
    admitted=admit(bytes.data(),encoded.Bytes,{remoteRequester});assert(admitted);
    admitted=admit(bytes.data(),encoded.Bytes,{remoteRequester});
    assert(admitted.Disposition==Primitive::PrimitiveAdmissionDisposition::AlreadyAccepted);
    assert(runtime.SourceSubscriberDirty<RuntimeState>(remoteRequester.Device));
    assert(runtime.ServiceLatest<RuntimeState>());
    assert(adapter.Last.Kind==S::StateMessageKind::Publication && adapter.Last.Snapshot.Value.Value==13);
    sourceResync.Resync=S::StateResyncToken{92};
    encoded=S::EncodeStateControl(sourceResync,bytes.data(),bytes.size());assert(encoded);
    admitted=admit(bytes.data(),encoded.Bytes,{remoteRequester});
    assert(admitted.Disposition==Primitive::PrimitiveAdmissionDisposition::Rejected);

    assert(runtime.RequireSourceResync<RuntimeState>(remoteRequester.Device)==S::StateRemoteStatus::Success);
    encoded=S::EncodeStateAcceptanceControl(sourceResyncAccepted,bytes.data(),bytes.size());assert(encoded);
    admitted=admit(bytes.data(),encoded.Bytes,{remoteRequester});
    assert(admitted.Disposition==Primitive::PrimitiveAdmissionDisposition::AlreadyAccepted);
    assert(runtime.GetSourceSubscriberStatus<RuntimeState>(remoteRequester.Device)==S::StateRemoteSessionState::ResyncRequired);
    adapter.Accept=false;
    assert(!runtime.ServiceLatest<RuntimeState>());
    assert(adapter.Last.Kind==S::StateMessageKind::ResyncRequired && !adapter.Last.HasSnapshot);
    adapter.Accept=true;
    assert(runtime.ServiceLatest<RuntimeState>());
    assert(adapter.Last.Owner==local && adapter.Last.Requester==remoteRequester);
    assert(adapter.Last.Session==S::StateSessionToken{41});
    assert(!runtime.ServiceLatest<RuntimeState>());

    // Requester side: source snapshot is committed before SubscribeAccepted is offered.
    const auto session=runtime.SubscribeFrom<RuntimeState>(remoteOwner.Device);assert(session);
    const S::StateSnapshot<RuntimeState> remoteInitial{{20},{200,Timing::TimeReliability::Synchronized}};
    S::StateControlWireHeader snapshotControl{S::StateMessageKind::SubscribeSnapshot,RuntimeState::TypeId,
        remoteOwner,local,session.Handle.Session,{},0};
    encoded=S::EncodeStateSnapshotControl<RuntimeState,Format>(snapshotControl,remoteInitial,{false,7},bytes.data(),bytes.size());assert(encoded);
    // Replica commit survives contention when transferring its protected ACK.
    AdmissionMutex::RejectAttempt=2;
    admitted=admit(bytes.data(),encoded.Bytes,{remoteOwner});
    assert(admitted.Disposition==Primitive::PrimitiveAdmissionDisposition::TemporarilyUnavailable);
    assert(runtime.GetRemoteSessionStatus<RuntimeState>(remoteOwner.Device)==S::StateRemoteSessionState::ActiveTrusted);
    AdmissionMutex::RejectAttempt=0;
    admitted=admit(bytes.data(),encoded.Bytes,{remoteOwner});
    assert(admitted && adapter.Last.Kind==S::StateMessageKind::SubscribeAccepted);
    S::StateSnapshot<RuntimeState> read{};
    assert(runtime.TryReadRemote<RuntimeState>(remoteOwner.Device,read) && read.Value.Value==20);
    assert(runtime.GetRemoteSessionStatus<RuntimeState>(remoteOwner.Device)==S::StateRemoteSessionState::ActiveTrusted);

    // A lost acceptance can cause an exact snapshot retry; it is re-acknowledged without mutation.
    admitted=admit(bytes.data(),encoded.Bytes,{remoteOwner});
    assert(admitted.Disposition==Primitive::PrimitiveAdmissionDisposition::AlreadyAccepted);
    assert(adapter.Last.Kind==S::StateMessageKind::SubscribeAccepted);

    const S::StateSnapshot<RuntimeState> remoteNext{{21},{210,Timing::TimeReliability::Synchronized}};
    std::array<std::uint8_t,S::MaximumCompleteStatePublicationWireBytes<RuntimeState,Format>> publication{};
    encoded=S::EncodeStatePublication<RuntimeState,Format>(remoteNext,{false,8},remoteOwner,local,session.Handle.Session,
                                                           publication.data(),publication.size());assert(encoded);
    admitted=admit(publication.data(),encoded.Bytes,{remoteOwner});
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
    admitted=admit(bytes.data(),encoded.Bytes,{remoteOwner});
    assert(admitted.Disposition==Primitive::PrimitiveAdmissionDisposition::Rejected);
    staleRequired=required;
    staleRequired.Owner.Incarnation=System::RuntimeIncarnationId{1};
    encoded=S::EncodeStateControl(staleRequired,bytes.data(),bytes.size());assert(encoded);
    admitted=admit(bytes.data(),encoded.Bytes,{staleRequired.Owner});
    assert(admitted.Disposition==Primitive::PrimitiveAdmissionDisposition::Rejected);
    assert(adapter.Admissions==admissionsBeforeStale);
    assert(runtime.GetRemoteSessionStatus<RuntimeState>(remoteOwner.Device)==S::StateRemoteSessionState::ActiveTrusted);
    assert(runtime.TryReadRemote<RuntimeState>(remoteOwner.Device,read) && read.Value.Value==21);
    encoded=S::EncodeStateControl(required,bytes.data(),bytes.size());assert(encoded);
    AdmissionMutex::RejectAttempt=2; // Table acquired; shared token authority busy.
    admitted=admit(bytes.data(),encoded.Bytes,{remoteOwner});
    assert(admitted.Disposition==Primitive::PrimitiveAdmissionDisposition::TemporarilyUnavailable);
    assert(runtime.GetRemoteSessionStatus<RuntimeState>(remoteOwner.Device)==S::StateRemoteSessionState::ActiveTrusted);
    AdmissionMutex::RejectAttempt=0;
    admitted=admit(bytes.data(),encoded.Bytes,{remoteOwner});assert(admitted);
    assert(adapter.Last.Kind==S::StateMessageKind::ResyncRequest && adapter.Last.Resync);
    const auto resync=adapter.Last.Resync;
    const S::StateSnapshot<RuntimeState> resynced{{30},{300,Timing::TimeReliability::Synchronized}};
    S::StateControlWireHeader resyncControl{S::StateMessageKind::ResyncSnapshot,RuntimeState::TypeId,
        remoteOwner,local,session.Handle.Session,resync,0};
    encoded=S::EncodeStateSnapshotControl<RuntimeState,Format>(resyncControl,resynced,{false,30},bytes.data(),bytes.size());assert(encoded);
    admitted=admit(bytes.data(),encoded.Bytes,{remoteOwner});assert(admitted);
    assert(adapter.Last.Kind==S::StateMessageKind::ResyncAccepted && adapter.Last.Resync==resync);
    admitted=admit(bytes.data(),encoded.Bytes,{remoteOwner});
    assert(admitted.Disposition==Primitive::PrimitiveAdmissionDisposition::AlreadyAccepted);
    assert(adapter.Last.Kind==S::StateMessageKind::ResyncAccepted);
    assert(runtime.TryReadRemote<RuntimeState>(remoteOwner.Device,read) && read.Value.Value==30);

    assert(runtime.Unsubscribe<RuntimeState>(session.Handle,S::StateReplicaRelease::RetainLastKnown)==S::StateRemoteStatus::Success);
    const auto admissionsAfterClose=adapter.Admissions;
    encoded=S::EncodeStateControl(required,bytes.data(),bytes.size());assert(encoded);
    admitted=admit(bytes.data(),encoded.Bytes,{remoteOwner});
    assert(admitted.Disposition==Primitive::PrimitiveAdmissionDisposition::Rejected);
    assert(adapter.Admissions==admissionsAfterClose);
    assert(runtime.GetRemoteSessionStatus<RuntimeState>(remoteOwner.Device)==S::StateRemoteSessionState::Inactive);
    assert(runtime.TryReadRemote<RuntimeState>(remoteOwner.Device,read) && read.Value.Value==30);

    // Selector mutation also refuses contention without consuming the rejection.
    const auto rejectedOwner=Identity(4,4);
    const auto rejectedSession=runtime.SubscribeFrom<RuntimeState>(rejectedOwner.Device);assert(rejectedSession);
    S::StateControlWireHeader rejected{S::StateMessageKind::SubscribeRejected,RuntimeState::TypeId,
        rejectedOwner,local,rejectedSession.Handle.Session,{},1};
    encoded=S::EncodeStateControl(rejected,bytes.data(),bytes.size());assert(encoded);
    for(unsigned attempt=1;attempt<=2;++attempt) {
        AdmissionMutex::RejectAttempt=attempt;
        admitted=admit(bytes.data(),encoded.Bytes,{rejectedOwner});
        assert(admitted.Disposition==Primitive::PrimitiveAdmissionDisposition::TemporarilyUnavailable);
    }
    AdmissionMutex::RejectAttempt=0;
    admitted=admit(bytes.data(),encoded.Bytes,{rejectedOwner});assert(admitted);
    assert(runtime.GetRemoteSessionStatus<RuntimeState>(rejectedOwner.Device)==S::StateRemoteSessionState::Inactive);

    assert(runtime.Shutdown()==S::StateRuntimeStatus::Success);
}
