#include <ESPressio_States.hpp>
#include <ESPressio_RuntimeIdentity.hpp>
#include <ESPressio_SerializationMacros.hpp>
#include <ESPressio_TypeDirectory.hpp>
#include <cassert>
#include <cstdint>
#include <string_view>

using namespace ESPressio;
namespace S=ESPressio::State;

struct Value final {
    std::uint32_t Number=0;
    constexpr bool operator==(const Value& other) const noexcept { return Number==other.Number; }
    ESPRESSIO_SERIALIZABLE_TYPE(Value)
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
struct TestState final : S::TransmissibleState<TestState,Value> {
    static constexpr S::StateTypeId TypeId{0x5502};
    static constexpr std::string_view CanonicalName="Test.State.ConvergenceFeedback";
    using ConvergencePolicy=Policy;
};

static System::DeviceRuntimeIdentity Identity(std::uint8_t marker) {
    System::DeviceIdentifier::Storage bytes{};bytes[0]=marker;
    return {System::DeviceIdentifier{bytes},System::RuntimeIncarnationId{1}};
}
static Timing::QualifiedTime Capture(){return {7,Timing::TimeReliability::Synchronized};}

struct Adapter final {
    unsigned Wakes=0,Admissions=0;
    S::StateOutboundMessage<TestState> Last{};
    void Wake() noexcept { ++Wakes; }
    bool Validate(const S::StateTransportContract&) noexcept { return true; }
    S::StateTransportAdmission Admit(const S::StateOutboundMessage<TestState>& message) noexcept {
        Last=message;++Admissions;return {S::StateTransportAdmissionStatus::Accepted};
    }
};

int main(){
    const auto local=Identity(1),remote=Identity(2);
    assert(System::RuntimeIdentity::Install(local)==System::RuntimeIdentity::InstallationStatus::Success);
    Primitive::TypeDirectory<1> directory;
    assert(directory.Register<TestState>()==Primitive::TypeDirectoryRegistrationStatus::Success);
    assert(directory.Initialize()==Primitive::TypeDirectoryInitializationStatus::Success);

    Adapter adapter;
    S::StateTransportBinding<TestState,Serializable::DirectBinary> binding;
    assert((binding.Initialize<Adapter,&Adapter::Admit,&Adapter::Validate,&Adapter::Wake>(adapter)));
    using Config=S::TypeConfiguration<TestState,S::MaximumRemoteOwners<1>,S::MaximumSubscribers<1>>;
    S::Runtime<Config> runtime;
    auto owner=runtime.BindOwner<TestState>();assert(owner);
    assert(runtime.BindTransport(binding)==S::StateRuntimeStatus::Success);
    assert(runtime.Initialize(directory.View(),&Capture)==S::StateRuntimeStatus::Success);
    assert(runtime.Start()==S::StateRuntimeStatus::Success);

    // Source-side ordinary convergence: an accepted family->adapter handoff transfers
    // campaign ownership. Terminal adapter exhaustion records only dormant semantic state.
    assert(owner.Set({10},Capture())==S::StateSetStatus::Changed);
    const S::StateSessionToken sourceSession{77};
    assert(runtime.ReserveSourceSubscriber<TestState>(remote,sourceSession)==S::StateRemoteStatus::Success);
    assert(runtime.ActivateSourceSubscriber<TestState>(remote,sourceSession,true,runtime.Version<TestState>())==S::StateRemoteStatus::Success);
    assert(owner.Set({20},Capture())==S::StateSetStatus::Changed);
    assert(runtime.ServiceLatest<TestState>());
    assert(adapter.Last.Kind==S::StateMessageKind::Publication && adapter.Last.Version==runtime.Version<TestState>());
    auto publication=adapter.Last.GetConvergenceHandle();
    assert(publication && publication.Kind==S::StateConvergenceWorkKind::Publication);
    assert(runtime.ReportConvergenceExhausted<TestState>(publication)==S::StateRemoteStatus::Success);
    assert(runtime.NeedsConvergence<TestState>(remote.Device,S::StateContinuitySide::SourceSubscriber));
    assert(!runtime.SourceSubscriberDirty<TestState>(remote.Device));
    assert(!runtime.ServiceLatest<TestState>());
    assert(runtime.ReportConvergenceExhausted<TestState>(publication)==S::StateRemoteStatus::Duplicate);

    const auto wakesBeforeUnavailable=adapter.Wakes;
    assert(runtime.ReportAvailabilityTransition<TestState>(false)==S::StateRemoteStatus::Success);
    assert(adapter.Wakes==wakesBeforeUnavailable);
    assert(runtime.NeedsConvergence<TestState>(remote.Device,S::StateContinuitySide::SourceSubscriber));
    assert(!runtime.ServiceLatest<TestState>());

    assert(runtime.ReportAvailabilityTransition<TestState>(true)==S::StateRemoteStatus::Success);
    assert(adapter.Wakes==wakesBeforeUnavailable+1);
    assert(!runtime.NeedsConvergence<TestState>(remote.Device,S::StateContinuitySide::SourceSubscriber));
    assert(runtime.ServiceLatest<TestState>());
    assert(adapter.Last.Kind==S::StateMessageKind::Publication && adapter.Last.Version==publication.Version);
    assert(runtime.AcceptSourceBaseline<TestState>(remote,sourceSession,publication.Version)==S::StateRemoteStatus::Success);

    // A newer authoritative fact supersedes dormant/older campaign feedback. A delayed
    // exhaustion callback for v3 must never dormant v4.
    assert(owner.Set({30},Capture())==S::StateSetStatus::Changed);
    assert(runtime.ServiceLatest<TestState>());
    const auto stale=adapter.Last.GetConvergenceHandle();assert(stale);
    assert(owner.Set({40},Capture())==S::StateSetStatus::Changed);
    assert(runtime.ReportConvergenceExhausted<TestState>(stale)==S::StateRemoteStatus::Older);
    assert(!runtime.NeedsConvergence<TestState>(remote.Device,S::StateContinuitySide::SourceSubscriber));
    assert(runtime.ServiceLatest<TestState>());
    assert(adapter.Last.Kind==S::StateMessageKind::Publication && adapter.Last.Version==runtime.Version<TestState>());

    // Protected source resync work also becomes dormant on terminal pursuit exhaustion
    // and is rearmed only by an explicit usable availability transition.
    assert(runtime.RequireSourceResync<TestState>(remote.Device)==S::StateRemoteStatus::Success);
    assert(runtime.ServiceLatest<TestState>());
    assert(adapter.Last.Kind==S::StateMessageKind::ResyncRequired);
    auto required=adapter.Last.GetConvergenceHandle();assert(required);
    assert(runtime.ReportConvergenceExhausted<TestState>(required)==S::StateRemoteStatus::Success);
    assert(runtime.NeedsConvergence<TestState>(remote.Device,S::StateContinuitySide::SourceSubscriber));
    assert(!runtime.ServiceLatest<TestState>());
    assert(runtime.ReportAvailabilityTransition<TestState>(true)==S::StateRemoteStatus::Success);
    assert(runtime.ServiceLatest<TestState>());
    assert(adapter.Last.Kind==S::StateMessageKind::ResyncRequired && adapter.Last.Version==required.Version);

    // Requester-side resync uses the same rule. Exhaustion invalidates the old token;
    // availability starts a fresh bounded campaign with a new process-wide token.
    const auto remoteSession=runtime.ReserveSubscriptionSession<TestState>(remote.Device);assert(remoteSession);
    assert(runtime.InstallSubscribeSnapshot<TestState>(remote,remoteSession.Handle.Session,{false,1},{{99},Capture()})==S::StateRemoteStatus::Success);
    S::StateContinuityHandle continuity{};
    assert(runtime.CaptureContinuity<TestState>(remote.Device,S::StateContinuitySide::RemoteOwner,continuity)==S::StateRemoteStatus::Success);
    assert(runtime.ReportContinuityLoss<TestState>(continuity)==S::StateRemoteStatus::Success);
    assert(runtime.ServiceLatest<TestState>());
    assert(adapter.Last.Kind==S::StateMessageKind::ResyncRequest && adapter.Last.Resync);
    auto request=adapter.Last.GetConvergenceHandle();assert(request);
    assert(runtime.ReportConvergenceExhausted<TestState>(request)==S::StateRemoteStatus::Success);
    assert(runtime.NeedsConvergence<TestState>(remote.Device,S::StateContinuitySide::RemoteOwner));
    assert(!runtime.ServiceLatest<TestState>());
    assert(runtime.ReportAvailabilityTransition<TestState>(true)==S::StateRemoteStatus::Success);
    assert(runtime.ServiceLatest<TestState>());
    assert(adapter.Last.Kind==S::StateMessageKind::ResyncRequest && adapter.Last.Resync && adapter.Last.Resync!=request.Resync);
    assert(runtime.ReportConvergenceExhausted<TestState>(request)==S::StateRemoteStatus::SessionMismatch);

    auto invalid=adapter.Last.GetConvergenceHandle();invalid.TypeId=S::StateTypeId{999};
    assert(runtime.ReportConvergenceExhausted<TestState>(invalid)==S::StateRemoteStatus::InvalidIdentity);
    invalid=adapter.Last.GetConvergenceHandle();invalid.Requester=remote;
    assert(runtime.ReportConvergenceExhausted<TestState>(invalid)==S::StateRemoteStatus::InvalidIdentity);

    assert(runtime.Shutdown()==S::StateRuntimeStatus::Success);
    assert(runtime.ReportAvailabilityTransition<TestState>(true)==S::StateRemoteStatus::NotRunning);
}
