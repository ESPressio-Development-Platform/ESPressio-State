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

static System::DeviceRuntimeIdentity Identity(std::uint8_t marker) {
    System::DeviceIdentifier::Storage bytes{};bytes[0]=marker;
    return {System::DeviceIdentifier{bytes},System::RuntimeIncarnationId{1}};
}
static Timing::QualifiedTime Capture(){return {1,Timing::TimeReliability::Synchronized};}
struct Adapter final {
    unsigned Wakes=0,Admissions=0;
    S::StateOutboundMessage<RuntimeState> Last{};
    void Wake() noexcept { ++Wakes; }
    bool Validate(const S::StateTransportContract&) noexcept { return true; }
    S::StateTransportAdmission Admit(const S::StateOutboundMessage<RuntimeState>& message) noexcept {
        Last=message;++Admissions;return {S::StateTransportAdmissionStatus::Accepted};
    }
};
int main(){
    const auto local=Identity(1),remote=Identity(2);
    assert(System::RuntimeIdentity::Install(local)==System::RuntimeIdentity::InstallationStatus::Success);
    Primitive::TypeDirectory<1> directory;
    assert(directory.Register<RuntimeState>()==Primitive::TypeDirectoryRegistrationStatus::Success);
    assert(directory.Initialize()==Primitive::TypeDirectoryInitializationStatus::Success);
    Adapter adapter;
    S::StateTransportBinding<RuntimeState,Serializable::DirectBinary> binding;
    assert((binding.Initialize<Adapter,&Adapter::Admit,&Adapter::Validate,&Adapter::Wake>(adapter)));
    S::Runtime<S::TypeConfiguration<RuntimeState,S::MaximumRemoteOwners<1>>> runtime;
    assert(runtime.BindTransport(binding)==S::StateRuntimeStatus::Success);
    assert(runtime.Initialize(directory.View(),&Capture)==S::StateRuntimeStatus::Success);
    assert(runtime.Start()==S::StateRuntimeStatus::Success);
    assert(!runtime.Version<RuntimeState>()); // No local owner or canonical fact.
    const auto session=runtime.ReserveSubscriptionSession<RuntimeState>(remote.Device);assert(session);
    assert(runtime.InstallSubscribeSnapshot<RuntimeState>(remote,session.Handle.Session,{false,1},{{42},Capture()})==S::StateRemoteStatus::Success);
    S::StateContinuityHandle handle{};
    assert(runtime.CaptureContinuity<RuntimeState>(remote.Device,S::StateContinuitySide::RemoteOwner,handle)==S::StateRemoteStatus::Success);
    auto invalid=handle;invalid.Side=static_cast<S::StateContinuitySide>(255);
    assert(runtime.ReportContinuityLoss<RuntimeState>(invalid)==S::StateRemoteStatus::InvalidIdentity);
    invalid=handle;invalid.TypeId=S::StateTypeId{123};
    assert(runtime.ReportContinuityLoss<RuntimeState>(invalid)==S::StateRemoteStatus::InvalidIdentity);
    assert(runtime.ReportContinuityLoss<RuntimeState>(handle)==S::StateRemoteStatus::Success);
    assert(adapter.Wakes==1);
    assert(runtime.ServiceLatest<RuntimeState>());
    assert(adapter.Last.Kind==S::StateMessageKind::ResyncRequest && !adapter.Last.HasSnapshot);
    assert(adapter.Last.Owner==remote && adapter.Last.Requester==local && adapter.Last.Session==session.Handle.Session && adapter.Last.Resync);
    assert(runtime.ReportContinuityLoss<RuntimeState>(handle)==S::StateRemoteStatus::Duplicate);
    assert(!runtime.ServiceLatest<RuntimeState>() && adapter.Admissions==1 && adapter.Wakes==1);
    assert(runtime.Unsubscribe<RuntimeState>(remote.Device,S::StateReplicaRelease::RetainLastKnown)==S::StateRemoteStatus::Success);
    assert(runtime.ReportContinuityLoss<RuntimeState>(handle)==S::StateRemoteStatus::SessionMismatch);
    S::StateSnapshot<RuntimeState> retained{};
    assert(runtime.TryReadRemote<RuntimeState>(remote.Device,retained) && retained.Value.Value==42);
    assert(runtime.Shutdown()==S::StateRuntimeStatus::Success);
}
