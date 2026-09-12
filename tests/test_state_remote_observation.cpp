#include <ESPressio_States.hpp>
#include <ESPressio_RuntimeIdentity.hpp>
#include <ESPressio_SerializationMacros.hpp>
#include <ESPressio_ThreadCapability.hpp>
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
struct ObservedState final:S::TransmissibleState<ObservedState,Value>{
    static constexpr S::StateTypeId TypeId{0x5703};
    static constexpr std::string_view CanonicalName="Test.State.RemoteObservation";
    using ConvergencePolicy=Policy;
};

static System::DeviceRuntimeIdentity Identity(std::uint8_t marker){
    System::DeviceIdentifier::Storage bytes{};bytes[0]=marker;
    return {System::DeviceIdentifier{bytes},System::RuntimeIncarnationId{1}};
}
static Timing::QualifiedTime Capture(){return {500,Timing::TimeReliability::Synchronized};}

struct Host final {
    unsigned Wakes=0;
    static bool Wake(void* p,bool) noexcept { ++static_cast<Host*>(p)->Wakes;return true; }
    static bool Accepts(const void*) noexcept { return true; }
};
struct Sink final {
    unsigned Calls=0;
    bool Saw=false;
    void Changed(const S::StateChangeSet& changes){++Calls;Saw=changes.Contains<ObservedState>();}
};

int main(){
    const auto local=Identity(1),remote=Identity(2);
    assert(System::RuntimeIdentity::Install(local)==System::RuntimeIdentity::InstallationStatus::Success);
    Primitive::TypeDirectory<1> directory;
    assert(directory.Register<ObservedState>()==Primitive::TypeDirectoryRegistrationStatus::Success);
    assert(directory.Initialize()==Primitive::TypeDirectoryInitializationStatus::Success);

    using Capability=S::ObserverCapability<ObservedState>;
    using Runtime=S::Runtime<S::TypeConfiguration<ObservedState,S::MaximumRemoteOwners<1>,S::MaximumSubscribers<0>>>;
    Runtime runtime;
    auto owner=runtime.BindOwner<ObservedState>();assert(owner);
    assert(runtime.Initialize(directory.View(),&Capture)==S::StateRuntimeStatus::Success);

    Host host;Sink sink;Capability capability;
    Threads::ThreadHostServices services{};
    services.Owner=&host;services.WakeFunction=&Host::Wake;services.AcceptingFunction=&Host::Accepts;
    assert(capability.OnChange(sink,&Sink::Changed));
    assert(capability.Initialize(services)==Threads::ThreadStatus::Success);
    assert(capability.FinalizeInitialization()==Threads::ThreadStatus::Success);
    assert(runtime.Start()==S::StateRuntimeStatus::Success);
    capability.Activate({0,services});

    // Remote session/replica mutation is infrastructure bookkeeping and retained
    // truth installation. It must not publish the local TH10 authoritative-State bit.
    const auto session=runtime.ReserveSubscriptionSession<ObservedState>(remote.Device);assert(session);
    S::StateSnapshot<ObservedState> first{{10},{100,Timing::TimeReliability::Holdover}};
    assert(runtime.InstallSubscribeSnapshot<ObservedState>(remote,session.Handle.Session,{false,1},first)==S::StateRemoteStatus::Success);
    assert(!capability.HasPending() && host.Wakes==0 && sink.Calls==0);

    S::StateSnapshot<ObservedState> second{{20},{200,Timing::TimeReliability::Acquiring}};
    assert(runtime.ApplyRemotePublication<ObservedState>(remote,session.Handle.Session,{false,2},second)==S::StateRemoteStatus::Success);
    assert(!capability.HasPending() && host.Wakes==0 && sink.Calls==0);
    S::StateSnapshot<ObservedState> read{};
    assert(runtime.TryReadRemote<ObservedState>(remote.Device,read));
    assert(read.Value.Number==20 && read.TruthTime.Nanoseconds==200);
    assert(runtime.GetRemoteSessionStatus<ObservedState>(remote.Device)==S::StateRemoteSessionState::ActiveTrusted);

    // Session mechanics likewise remain non-callback. Last-known truth remains
    // readable after local session closure, independently of any reachability concept.
    assert(runtime.Unsubscribe<ObservedState>(remote.Device,S::StateReplicaRelease::RetainLastKnown)==S::StateRemoteStatus::Success);
    assert(!capability.HasPending() && host.Wakes==0 && sink.Calls==0);
    assert(runtime.GetRemoteSessionStatus<ObservedState>(remote.Device)==S::StateRemoteSessionState::Inactive);
    assert(runtime.TryReadRemote<ObservedState>(remote.Device,read) && read.Value.Number==20);

    // The same capability is still the ordinary TH10 observation mechanism for
    // local authoritative commits and runs application code only in its service pass.
    assert(owner.Set({99},{999,Timing::TimeReliability::Synchronized})==S::StateSetStatus::Changed);
    assert(capability.HasPending() && host.Wakes==1 && sink.Calls==0);
    capability.Service({0,services});
    assert(sink.Calls==1 && sink.Saw && !capability.HasPending());

    capability.Quiesce({0,services});
    assert(runtime.Shutdown()==S::StateRuntimeStatus::Success);
}
