#include <ESPressio_States.hpp>
#include <ESPressio_RuntimeIdentity.hpp>
#include <ESPressio_SerializationMacros.hpp>
#include <ESPressio_TypeDirectory.hpp>
#include <cassert>
#include <cstdint>
#include <string_view>

using namespace ESPressio;
namespace S=ESPressio::State;

struct RetainedValue final {
    std::uint32_t Reading=0;
    constexpr bool operator==(const RetainedValue& other) const noexcept { return Reading==other.Reading; }
    ESPRESSIO_SERIALIZABLE_TYPE(RetainedValue)
    ESPRESSIO_SERIALIZABLE_SCHEMA_VERSION(1)
    ESPRESSIO_SERIALIZABLE_PROPERTIES(ESPRESSIO_PROPERTY("reading",Reading))
};
struct RetainedPolicy final {
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
struct RetainedState final:S::TransmissibleState<RetainedState,RetainedValue> {
    static constexpr S::StateTypeId TypeId{0x1601};
    static constexpr std::string_view CanonicalName="Example.State.RetainedRemote";
    using ConvergencePolicy=RetainedPolicy;
};

static System::DeviceRuntimeIdentity Identity(std::uint8_t marker) {
    System::DeviceIdentifier::Storage bytes{};bytes[0]=marker;
    return {System::DeviceIdentifier{bytes},System::RuntimeIncarnationId{1}};
}

Primitive::TypeDirectory<1> directory;
S::Runtime<S::TypeConfiguration<RetainedState,S::MaximumRemoteOwners<1>,S::MaximumSubscribers<0>>> states;

void setup() {
    assert(System::RuntimeIdentity::Install(Identity(1))==System::RuntimeIdentity::InstallationStatus::Success);
    (void)directory.Register<RetainedState>();
    (void)directory.Initialize();
    assert(states.Initialize(directory.View())==S::StateRuntimeStatus::Success);
    assert(states.Start()==S::StateRuntimeStatus::Success);

    const auto remote=Identity(2);
    const auto reserved=states.ReserveSubscriptionSession<RetainedState>(remote.Device);
    assert(reserved);
    S::StateSnapshot<RetainedState> incoming{{55},{500,Timing::TimeReliability::Synchronized}};
    assert(states.InstallSubscribeSnapshot<RetainedState>(remote,reserved.Handle.Session,{false,1},incoming)==S::StateRemoteStatus::Success);

    assert(states.Unsubscribe<RetainedState>(remote.Device,S::StateReplicaRelease::RetainLastKnown)==S::StateRemoteStatus::Success);
    assert(states.GetRemoteSessionStatus<RetainedState>(remote.Device)==S::StateRemoteSessionState::Inactive);

    S::StateSnapshot<RetainedState> retained{};
    assert(states.TryReadRemote<RetainedState>(remote.Device,retained));
    assert(retained.Value.Reading==55); // session state and last-known truth are deliberately separate.

    assert(states.ForgetRemote<RetainedState>(remote.Device)==S::StateRemoteStatus::Success);
    assert(!states.TryReadRemote<RetainedState>(remote.Device,retained));
}

void loop() {}
