#include <ESPressio_States.hpp>
#include <ESPressio_RuntimeIdentity.hpp>
#include <ESPressio_SerializationMacros.hpp>
#include <ESPressio_TypeDirectory.hpp>
#include <cassert>
#include <cstdint>
#include <string_view>
using namespace ESPressio;
namespace S=ESPressio::State;

struct RemoteValue final {
    std::uint32_t Value=0;
    constexpr bool operator==(const RemoteValue& other) const noexcept { return Value==other.Value; }
    ESPRESSIO_SERIALIZABLE_TYPE(RemoteValue)
    ESPRESSIO_SERIALIZABLE_SCHEMA_VERSION(1)
    ESPRESSIO_SERIALIZABLE_PROPERTIES(ESPRESSIO_PROPERTY("value",Value))
};
struct AckPolicy final {
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
struct BestEffortPolicy final {
    using PolicyCategory=Primitive::StateConvergencePolicyTag;
    using RequiredEvidence=Primitive::NoRemoteEvidence;
    using Supersession=Primitive::LatestAuthoritativeValue;
    using ExhaustionDisposition=Primitive::DormantNeedsConvergence;
    static constexpr std::uint64_t MaximumResidenceNanoseconds=1000000000ULL;
    static constexpr std::uint64_t MaximumAdapterAdmissionWaitNanoseconds=1000000ULL;
    static constexpr std::uint16_t MaximumAttempts=2;
    static constexpr std::uint64_t MinimumRetrySpacingNanoseconds=1000ULL;
    static constexpr std::uint64_t MaximumRetrySpacingNanoseconds=1000000ULL;
};
struct AckState final : S::TransmissibleState<AckState,RemoteValue> {
    static constexpr S::StateTypeId TypeId{0x5201};
    static constexpr std::string_view CanonicalName="Test.State.Acknowledged";
    using ConvergencePolicy=AckPolicy;
};
struct BestState final : S::TransmissibleState<BestState,RemoteValue> {
    static constexpr S::StateTypeId TypeId{0x5202};
    static constexpr std::string_view CanonicalName="Test.State.BestEffort";
    using ConvergencePolicy=BestEffortPolicy;
};
struct OtherRuntimeState final : S::TransmissibleState<OtherRuntimeState,RemoteValue> {
    static constexpr S::StateTypeId TypeId{0x5203};
    static constexpr std::string_view CanonicalName="Test.State.OtherRuntime";
    using ConvergencePolicy=AckPolicy;
};

static Timing::QualifiedTime Capture(){return {55,Timing::TimeReliability::Synchronized};}
static System::DeviceRuntimeIdentity Identity(std::uint8_t marker,std::uint32_t runtime){
    System::DeviceIdentifier::Storage bytes{};bytes[0]=marker;return {System::DeviceIdentifier{bytes},System::RuntimeIncarnationId{runtime}};
}
static S::StateSnapshot<AckState> AckSnapshot(std::uint32_t value,std::uint64_t time){return {{value},{time,Timing::TimeReliability::Synchronized}};}

int main(){
    assert(System::RuntimeIdentity::Install(Identity(1,1))==System::RuntimeIdentity::InstallationStatus::Success);
    Primitive::TypeDirectory<2> directory;
    assert(directory.Register<AckState>()==Primitive::TypeDirectoryRegistrationStatus::Success);
    assert(directory.Register<BestState>()==Primitive::TypeDirectoryRegistrationStatus::Success);
    assert(directory.Initialize()==Primitive::TypeDirectoryInitializationStatus::Success);
    using Runtime=S::Runtime<
        S::TypeConfiguration<AckState,S::MaximumRemoteOwners<2>,S::MaximumSubscribers<2>>,
        S::TypeConfiguration<BestState,S::MaximumRemoteOwners<1>,S::MaximumSubscribers<1>>>;
    static_assert(Runtime::MaximumRemoteOwnersFor<AckState>()==2);
    static_assert(Runtime::MaximumSubscribersFor<AckState>()==2);
    static_assert(Runtime::MaximumRemoteOwnersFor<BestState>()==1);
    Runtime runtime;
    auto ackOwner=runtime.BindOwner<AckState>();assert(ackOwner);
    auto bestOwner=runtime.BindOwner<BestState>();assert(bestOwner);
    assert(runtime.Initialize(directory.View(),&Capture)==S::StateRuntimeStatus::Success);
    assert(runtime.Start()==S::StateRuntimeStatus::Success);

    const auto owner1=Identity(0x21,11),owner2=Identity(0x22,12),owner3=Identity(0x23,13);
    const auto sub1=runtime.ReserveSubscriptionSession<AckState>(owner1.Device);
    const auto sub2=runtime.ReserveSubscriptionSession<AckState>(owner2.Device);
    assert(sub1 && sub2 && sub1.Handle.Session.Value()==1 && sub2.Handle.Session.Value()==2);
    const auto saturated=runtime.ReserveSubscriptionSession<AckState>(owner3.Device);
    assert(!saturated && saturated.Status==S::StateRemoteStatus::CapacityUnavailable);
    assert(runtime.RemoteOwnersInUse<AckState>()==2);

    const auto initial=AckSnapshot(10,100);
    assert(runtime.InstallSubscribeSnapshot<AckState>(owner1,sub1.Handle.Session,{false,10},initial)==S::StateRemoteStatus::Success);
    assert(runtime.GetRemoteSessionStatus<AckState>(owner1.Device)==S::StateRemoteSessionState::ActiveTrusted);
    assert(runtime.ForgetRemote<AckState>(owner1.Device)==S::StateRemoteStatus::Conflict);
    assert(runtime.RemoteOwnersInUse<AckState>()==2);
    assert(runtime.ApplyRemotePublication<AckState>(owner1,sub1.Handle.Session,{false,10},initial)==S::StateRemoteStatus::Duplicate);
    assert(runtime.ApplyRemotePublication<AckState>(owner1,sub1.Handle.Session,{false,9},AckSnapshot(9,90))==S::StateRemoteStatus::Older);
    const auto newer=AckSnapshot(11,110);
    assert(runtime.ApplyRemotePublication<AckState>(owner1,sub1.Handle.Session,{false,11},newer)==S::StateRemoteStatus::Success);

    // Same compact version with different semantic content destroys trust rather than applying owner comparison/deadband.
    assert(runtime.ApplyRemotePublication<AckState>(owner1,sub1.Handle.Session,{false,11},AckSnapshot(12,110))==S::StateRemoteStatus::ResyncRequired);
    S::StateSnapshot<AckState> retained{};
    assert(runtime.TryReadRemote<AckState>(owner1.Device,retained) && retained.Value.Value==11);

    S::StateResyncToken r1{},r2{};
    assert(runtime.AllocateResyncToken(r1)==S::StateRemoteStatus::Success && r1.Value()==1);
    assert(runtime.BeginRemoteResync<AckState>(owner1.Device,r1)==S::StateRemoteStatus::Success);
    assert(runtime.ApplyRemotePublication<AckState>(owner1,sub1.Handle.Session,{false,12},AckSnapshot(12,120))==S::StateRemoteStatus::AwaitingResync);
    assert(runtime.TryReadRemote<AckState>(owner1.Device,retained) && retained.Value.Value==11);
    assert(runtime.AllocateResyncToken(r2)==S::StateRemoteStatus::Success && r2.Value()==2);
    assert(runtime.BeginRemoteResync<AckState>(owner1.Device,r2)==S::StateRemoteStatus::Success);
    assert(runtime.InstallResyncSnapshot<AckState>(owner1,sub1.Handle.Session,r1,{false,20},AckSnapshot(20,200))==S::StateRemoteStatus::SessionMismatch);
    assert(runtime.InstallResyncSnapshot<AckState>(owner1,sub1.Handle.Session,r2,{false,20},AckSnapshot(20,200))==S::StateRemoteStatus::Success);

    // Exact half-range is ambiguous even with a syntactically valid compact version.
    assert(runtime.ApplyRemotePublication<AckState>(owner1,sub1.Handle.Session,{true,20},AckSnapshot(99,999))==S::StateRemoteStatus::ResyncRequired);
    assert(runtime.TryReadRemote<AckState>(owner1.Device,retained) && retained.Value.Value==20);
    assert(runtime.Unsubscribe<AckState>(owner1.Device,S::StateReplicaRelease::RetainLastKnown)==S::StateRemoteStatus::Success);
    assert(runtime.GetRemoteSessionStatus<AckState>(owner1.Device)==S::StateRemoteSessionState::Inactive);
    assert(runtime.TryReadRemote<AckState>(owner1.Device,retained) && retained.Value.Value==20);
    assert(runtime.ForgetRemote<AckState>(owner1.Device)==S::StateRemoteStatus::Success);
    assert(runtime.RemoteOwnersInUse<AckState>()==1);
    const auto afterBurn=runtime.ReserveSubscriptionSession<AckState>(owner3.Device);
    assert(afterBurn && afterBurn.Handle.Session.Value()==4); // token 3 was burned by the capacity-rejected attempt.

    // Token generators never wrap/reuse.
    S::StateSessionTokenGenerator exhausted(UINT32_MAX-1);S::StateSessionToken last{};
    assert(exhausted.TryAllocate(last) && last.Value()==UINT32_MAX);assert(!exhausted.TryAllocate(last));
    S::StateResyncTokenGenerator exhaustedResync(UINT32_MAX);S::StateResyncToken none{};assert(!exhaustedResync.TryAllocate(none));

    const auto requester1=Identity(0x31,21),requester2=Identity(0x32,22),requester3=Identity(0x33,23);
    assert(runtime.ReserveSourceSubscriber<AckState>(requester1,S::StateSessionToken{101})==S::StateRemoteStatus::Success);
    assert(runtime.ReserveSourceSubscriber<AckState>(requester2,S::StateSessionToken{102})==S::StateRemoteStatus::Success);
    assert(runtime.ReserveSourceSubscriber<AckState>(requester3,S::StateSessionToken{103})==S::StateRemoteStatus::CapacityUnavailable);
    assert(runtime.SubscribersInUse<AckState>()==2);
    assert(runtime.ActivateSourceSubscriber<AckState>(requester1,S::StateSessionToken{101},true,{false,1})==S::StateRemoteStatus::Success);
    assert(ackOwner.Set({1},{1,Timing::TimeReliability::Synchronized})==S::StateSetStatus::Changed);
    assert(runtime.SourceSubscriberDirty<AckState>(requester1.Device));
    assert(runtime.AcceptSourceBaseline<AckState>(requester1,S::StateSessionToken{101},runtime.Version<AckState>())==S::StateRemoteStatus::Success);
    assert(!runtime.SourceSubscriberDirty<AckState>(requester1.Device));

    // Acknowledged convergence remains ordinary through the uint16 wrap, then forces resync at exact half-range.
    for(std::uint32_t value=2;value<=65536;++value)
        assert(ackOwner.Set({value},{value,Timing::TimeReliability::Synchronized})==S::StateSetStatus::Changed);
    assert((runtime.Version<AckState>()==S::StateVersion{true,0}));
    assert(runtime.GetSourceSubscriberStatus<AckState>(requester1.Device)==S::StateRemoteSessionState::ActiveTrusted);
    assert(ackOwner.Set({65537},{65537,Timing::TimeReliability::Synchronized})==S::StateSetStatus::Changed);
    assert((runtime.Version<AckState>()==S::StateVersion{true,1}));
    assert(runtime.GetSourceSubscriberStatus<AckState>(requester1.Device)==S::StateRemoteSessionState::ResyncRequired);
    assert(!runtime.SourceSubscriberDirty<AckState>(requester1.Device));

    const auto bestRequester=Identity(0x41,31);
    assert(runtime.ReserveSourceSubscriber<BestState>(bestRequester,S::StateSessionToken{201})==S::StateRemoteStatus::Success);
    assert(runtime.ActivateSourceSubscriber<BestState>(bestRequester,S::StateSessionToken{201},true,{false,1})==S::StateRemoteStatus::Success);
    assert(bestOwner.Set({1},{1,Timing::TimeReliability::Synchronized})==S::StateSetStatus::Changed);
    assert(runtime.AcceptSourceBaseline<BestState>(bestRequester,S::StateSessionToken{201},runtime.Version<BestState>())==S::StateRemoteStatus::Success);
    for(std::uint32_t value=2;value<=65536;++value)
        assert(bestOwner.Set({value},{value,Timing::TimeReliability::Synchronized})==S::StateSetStatus::Changed);
    assert((runtime.Version<BestState>()==S::StateVersion{true,0}));
    assert(runtime.GetSourceSubscriberStatus<BestState>(bestRequester.Device)==S::StateRemoteSessionState::ResyncRequired);
    assert(!runtime.SourceSubscriberDirty<BestState>(bestRequester.Device));

    // Disjoint family Runtime compositions still share the current process's token namespace.
    // Neither a new Type pack nor another Runtime instance may restart session/resync numbering.
    Primitive::TypeDirectory<1> otherDirectory;
    assert(otherDirectory.Register<OtherRuntimeState>()==Primitive::TypeDirectoryRegistrationStatus::Success);
    assert(otherDirectory.Initialize()==Primitive::TypeDirectoryInitializationStatus::Success);
    S::Runtime<S::TypeConfiguration<OtherRuntimeState,S::MaximumRemoteOwners<1>>> otherRuntime;
    assert(otherRuntime.Initialize(otherDirectory.View(),&Capture)==S::StateRuntimeStatus::Success);
    assert(otherRuntime.Start()==S::StateRuntimeStatus::Success);
    const auto otherSession=otherRuntime.ReserveSubscriptionSession<OtherRuntimeState>(owner1.Device);
    assert(otherSession && otherSession.Handle.Session.Value()==5);
    S::StateResyncToken otherResync{},originalResync{};
    assert(otherRuntime.AllocateResyncToken(otherResync)==S::StateRemoteStatus::Success && otherResync.Value()==3);
    assert(runtime.AllocateResyncToken(originalResync)==S::StateRemoteStatus::Success && originalResync.Value()==4);
    assert(runtime.Unsubscribe<AckState>(owner2.Device,S::StateReplicaRelease::ReleaseReplica)==S::StateRemoteStatus::Success);
    const auto originalSession=runtime.ReserveSubscriptionSession<AckState>(owner2.Device);
    assert(originalSession && originalSession.Handle.Session.Value()==6);
    assert(otherRuntime.Shutdown()==S::StateRuntimeStatus::Success);
    assert(runtime.Shutdown()==S::StateRuntimeStatus::Success);
}
