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

struct SelectorValue final {
    std::uint32_t Value=0;
    constexpr bool operator==(const SelectorValue& other) const noexcept { return Value==other.Value; }
    ESPRESSIO_SERIALIZABLE_TYPE(SelectorValue)
    ESPRESSIO_SERIALIZABLE_SCHEMA_VERSION(1)
    ESPRESSIO_SERIALIZABLE_PROPERTIES(ESPRESSIO_PROPERTY("value",Value))
};
struct SelectorPolicy final {
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
struct SpecificState final : S::TransmissibleState<SpecificState,SelectorValue> {
    static constexpr S::StateTypeId TypeId{0x5501};
    static constexpr std::string_view CanonicalName="Test.State.SubscribeSpecific";
    using ConvergencePolicy=SelectorPolicy;
};
struct AnyState final : S::TransmissibleState<AnyState,SelectorValue> {
    static constexpr S::StateTypeId TypeId{0x5502};
    static constexpr std::string_view CanonicalName="Test.State.SubscribeAny";
    using ConvergencePolicy=SelectorPolicy;
};

static System::DeviceRuntimeIdentity Identity(std::uint8_t marker,std::uint32_t runtime){
    System::DeviceIdentifier::Storage bytes{};bytes[0]=marker;
    return {System::DeviceIdentifier{bytes},System::RuntimeIncarnationId{runtime}};
}
static Timing::QualifiedTime Capture(){return {100,Timing::TimeReliability::Synchronized};}

template<class TState>
struct Adapter final {
    unsigned Wakes=0;
    void Wake() noexcept { ++Wakes; }
    bool RejectNext=false;
    std::array<System::DeviceIdentifier,3> Discovered{};
    std::size_t DiscoveryCount=0;
    std::array<S::StateOutboundMessage<TState>,16> Messages{};
    std::size_t MessageCount=0;
    void* ProbeContext=nullptr;
    void (*Probe)(void*,const S::StateOutboundMessage<TState>&)=nullptr;

    bool Validate(const S::StateTransportContract&) noexcept { return true; }
    S::StateTransportAdmission Admit(const S::StateOutboundMessage<TState>& message) noexcept {
        if(Probe) Probe(ProbeContext,message);
        if(RejectNext){RejectNext=false;return {S::StateTransportAdmissionStatus::CapacityUnavailable};}
        assert(MessageCount<Messages.size());Messages[MessageCount++]=message;
        return {S::StateTransportAdmissionStatus::Accepted};
    }
    S::StateOwnerDiscoveryStatus Discover(S::StateOwnerDiscoverySink sink) noexcept {
        for(std::size_t i=0;i<DiscoveryCount;++i)
            if(!sink.TryOffer(Discovered[i])) return S::StateOwnerDiscoveryStatus::CapacityUnavailable;
        return S::StateOwnerDiscoveryStatus::Success;
    }
};

int main(){
    const auto local=Identity(1,1);
    assert(System::RuntimeIdentity::Install(local)==System::RuntimeIdentity::InstallationStatus::Success);
    Primitive::TypeDirectory<2> directory;
    assert(directory.Register<SpecificState>()==Primitive::TypeDirectoryRegistrationStatus::Success);
    assert(directory.Register<AnyState>()==Primitive::TypeDirectoryRegistrationStatus::Success);
    assert(directory.Initialize()==Primitive::TypeDirectoryInitializationStatus::Success);

    Adapter<SpecificState> specificAdapter;
    S::StateTransportBinding<SpecificState,Serializable::DirectBinary> specificBinding;
    assert((specificBinding.InitializeWithDiscovery<Adapter<SpecificState>,&Adapter<SpecificState>::Admit,&Adapter<SpecificState>::Validate,&Adapter<SpecificState>::Wake,&Adapter<SpecificState>::Discover>(specificAdapter)));

    Adapter<AnyState> anyAdapter;
    anyAdapter.Discovered={Identity(0x31,31).Device,Identity(0x32,32).Device,Identity(0x33,33).Device};
    anyAdapter.DiscoveryCount=2;
    S::StateTransportBinding<AnyState,Serializable::DirectBinary> anyBinding;
    assert((anyBinding.InitializeWithDiscovery<Adapter<AnyState>,&Adapter<AnyState>::Admit,&Adapter<AnyState>::Validate,&Adapter<AnyState>::Wake,&Adapter<AnyState>::Discover>(anyAdapter)));

    using Runtime=S::Runtime<
        S::TypeConfiguration<SpecificState,S::MaximumRemoteOwners<2>,S::MaximumSubscribers<1>>,
        S::TypeConfiguration<AnyState,S::MaximumRemoteOwners<2>,S::MaximumSubscribers<1>>>;
    Runtime runtime;
    assert(runtime.BindTransport(specificBinding)==S::StateRuntimeStatus::Success);
    assert(runtime.BindTransport(anyBinding)==S::StateRuntimeStatus::Success);
    assert(runtime.Initialize(directory.View(),&Capture)==S::StateRuntimeStatus::Success);
    assert(runtime.Start()==S::StateRuntimeStatus::Success);

    const auto deviceA=Identity(0x21,21);
    const auto first=runtime.SubscribeFrom<SpecificState>(deviceA.Device);
    assert(first && first.Handle.Session.Value()==1);
    assert(runtime.SubscriptionSelectorMode<SpecificState>()==S::StateSubscriptionSelectorMode::SpecificDevice);
    assert(specificAdapter.MessageCount==1);
    const auto& request=specificAdapter.Messages[0];
    assert(request.Kind==S::StateMessageKind::SubscribeRequest);
    assert(request.Owner.Device==deviceA.Device && !request.Owner.Incarnation);
    assert(request.Requester==local && request.Session==first.Handle.Session && !request.HasSnapshot);

    const S::StateSnapshot<SpecificState> retained{{77},{777,Timing::TimeReliability::Synchronized}};
    assert(runtime.InstallSubscribeSnapshot<SpecificState>(deviceA,first.Handle.Session,{false,1},retained)==S::StateRemoteStatus::Success);
    assert(runtime.Unsubscribe<SpecificState>(first.Handle,S::StateReplicaRelease::RetainLastKnown)==S::StateRemoteStatus::Success);
    assert(runtime.SubscriptionSelectorMode<SpecificState>()==S::StateSubscriptionSelectorMode::None);
    S::StateSnapshot<SpecificState> read{};
    assert(runtime.TryReadRemote<SpecificState>(deviceA.Device,read) && read.Value.Value==77);

    specificAdapter.RejectNext=true;
    const auto rejected=runtime.SubscribeFrom<SpecificState>(deviceA.Device);
    assert(!rejected && rejected.Status==S::StateRemoteStatus::CapacityUnavailable);
    assert(runtime.SubscriptionSelectorMode<SpecificState>()==S::StateSubscriptionSelectorMode::None);
    assert(runtime.TryReadRemote<SpecificState>(deviceA.Device,read) && read.Value.Value==77);

    const auto deviceB=Identity(0x22,22);
    const auto second=runtime.SubscribeFrom<SpecificState>(deviceB.Device);
    assert(second && second.Handle.Session.Value()==3); // token 2 was burned by rejected adapter admission.
    const auto anyConflict=runtime.SubscribeAny<SpecificState>();
    assert(!anyConflict && anyConflict.Status==S::StateRemoteStatus::Conflict && anyConflict.SessionCount==0);
    assert(runtime.Unsubscribe<SpecificState>(second.Handle,S::StateReplicaRelease::ReleaseReplica)==S::StateRemoteStatus::Success);
    assert(runtime.SubscriptionSelectorMode<SpecificState>()==S::StateSubscriptionSelectorMode::None);

    const auto any=runtime.SubscribeAny<AnyState>();
    assert(any && any.SessionCount==2);
    assert(any.Sessions[0].Session.Value()==4 && any.Sessions[1].Session.Value()==5);
    assert(runtime.SubscriptionSelectorMode<AnyState>()==S::StateSubscriptionSelectorMode::AnyDevice);
    assert(anyAdapter.MessageCount==2);
    for(std::size_t i=0;i<2;++i){
        assert(anyAdapter.Messages[i].Kind==S::StateMessageKind::SubscribeRequest);
        assert(anyAdapter.Messages[i].Owner.Device==anyAdapter.Discovered[i]);
        assert(anyAdapter.Messages[i].Requester==local);
    }
    const auto specificConflict=runtime.SubscribeFrom<AnyState>(Identity(0x40,40).Device);
    assert(!specificConflict && specificConflict.Status==S::StateRemoteStatus::Conflict);
    const auto expansion=runtime.ExpandAnyTo<AnyState>(Identity(0x40,40).Device);
    assert(!expansion && expansion.Status==S::StateRemoteStatus::CapacityUnavailable); // token 6 is burned before table admission.

    assert(runtime.Unsubscribe<AnyState>(any.Sessions[0],S::StateReplicaRelease::ReleaseReplica)==S::StateRemoteStatus::Success);
    assert(runtime.SubscriptionSelectorMode<AnyState>()==S::StateSubscriptionSelectorMode::AnyDevice);
    assert(runtime.Unsubscribe<AnyState>(any.Sessions[1],S::StateReplicaRelease::ReleaseReplica)==S::StateRemoteStatus::Success);
    assert(runtime.SubscriptionSelectorMode<AnyState>()==S::StateSubscriptionSelectorMode::None);

    // Discovery exceeding the declared owner capacity is rejected before any new SubscribeRequest is emitted.
    anyAdapter.DiscoveryCount=3;
    const auto messagesBeforeOverflow=anyAdapter.MessageCount;
    const auto overflow=runtime.SubscribeAny<AnyState>();
    assert(!overflow && overflow.Status==S::StateRemoteStatus::CapacityUnavailable && overflow.SessionCount==0);
    assert(anyAdapter.MessageCount==messagesBeforeOverflow);
    assert(runtime.SubscriptionSelectorMode<AnyState>()==S::StateSubscriptionSelectorMode::None);

    const auto afterOverflow=runtime.SubscribeFrom<AnyState>(Identity(0x40,40).Device);
    assert(afterOverflow && afterOverflow.Handle.Session.Value()==7); // discovery itself allocates no session tokens.
    assert(runtime.Unsubscribe<AnyState>(afterOverflow.Handle,S::StateReplicaRelease::ReleaseReplica)==S::StateRemoteStatus::Success);

    // Local closure precedes adapter admission, even when the remote notification is rejected.
    const auto closing=runtime.SubscribeFrom<AnyState>(deviceA.Device);
    assert(closing);
    const S::StateSnapshot<AnyState> closingSnapshot{{88},{888,Timing::TimeReliability::Holdover}};
    assert(runtime.InstallSubscribeSnapshot<AnyState>(deviceA,closing.Handle.Session,{false,1},closingSnapshot)==S::StateRemoteStatus::Success);
    struct ClosureProbe { Runtime* RuntimeOwner; System::DeviceRuntimeIdentity Owner; bool Called=false; } probe{&runtime,deviceA};
    anyAdapter.ProbeContext=&probe;
    anyAdapter.Probe=[](void* context,const S::StateOutboundMessage<AnyState>& message){
        if(message.Kind!=S::StateMessageKind::UnsubscribeRequest) return;
        auto& check=*static_cast<ClosureProbe*>(context);
        assert(message.Owner==check.Owner);
        assert(check.RuntimeOwner->GetRemoteSessionStatus<AnyState>(check.Owner.Device)==S::StateRemoteSessionState::Inactive);
        assert(check.RuntimeOwner->SubscriptionSelectorMode<AnyState>()==S::StateSubscriptionSelectorMode::None);
        check.Called=true;
    };
    anyAdapter.RejectNext=true;
    assert(runtime.Unsubscribe<AnyState>(closing.Handle,S::StateReplicaRelease::RetainLastKnown)==S::StateRemoteStatus::Success);
    assert(probe.Called);
    const S::StateSnapshot<AnyState> late{{99},{999,Timing::TimeReliability::Synchronized}};
    assert(runtime.ApplyRemotePublication<AnyState>(deviceA,closing.Handle.Session,{false,2},late)==S::StateRemoteStatus::SessionMismatch);
    S::StateSnapshot<AnyState> closedRead{};
    assert(runtime.TryReadRemote<AnyState>(deviceA.Device,closedRead) && closedRead.Value.Value==88);
    assert(closedRead.TruthTime.Nanoseconds==888 && closedRead.TruthTime.Reliability==Timing::TimeReliability::Holdover);
    anyAdapter.Probe=nullptr;

    // A stale handle cannot close a replacement session for the same owner.
    const auto replacement=runtime.SubscribeFrom<AnyState>(deviceA.Device);
    assert(replacement && replacement.Handle.Session!=closing.Handle.Session);
    assert(runtime.Unsubscribe<AnyState>(closing.Handle,S::StateReplicaRelease::ReleaseReplica)==S::StateRemoteStatus::SessionMismatch);
    assert(runtime.GetRemoteSessionStatus<AnyState>(deviceA.Device)==S::StateRemoteSessionState::Establishing);
    assert(runtime.InstallSubscribeSnapshot<AnyState>(deviceA,replacement.Handle.Session,{false,2},closingSnapshot)==S::StateRemoteStatus::Success);
    anyAdapter.RejectNext=true;
    assert(runtime.Unsubscribe<AnyState>(replacement.Handle,S::StateReplicaRelease::ReleaseReplica)==S::StateRemoteStatus::Success);
    assert(!runtime.TryReadRemote<AnyState>(deviceA.Device,closedRead));

    // Repeated discovery of an existing owner consumes no capacity, including at the exact limit.
    anyAdapter.Discovered[2]=anyAdapter.Discovered[0];
    anyAdapter.DiscoveryCount=3;
    const auto deduplicated=runtime.SubscribeAny<AnyState>();
    assert(deduplicated && deduplicated.SessionCount==2);
    for(std::size_t i=0;i<deduplicated.SessionCount;++i)
        assert(runtime.Unsubscribe<AnyState>(deduplicated.Sessions[i],S::StateReplicaRelease::ReleaseReplica)==S::StateRemoteStatus::Success);

    assert(runtime.Shutdown()==S::StateRuntimeStatus::Success);
}
