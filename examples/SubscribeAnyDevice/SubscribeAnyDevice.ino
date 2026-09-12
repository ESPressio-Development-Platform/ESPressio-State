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

struct FleetValue final {
    std::uint32_t Reading=0;
    constexpr bool operator==(const FleetValue& other) const noexcept { return Reading==other.Reading; }
    ESPRESSIO_SERIALIZABLE_TYPE(FleetValue)
    ESPRESSIO_SERIALIZABLE_SCHEMA_VERSION(1)
    ESPRESSIO_SERIALIZABLE_PROPERTIES(ESPRESSIO_PROPERTY("reading",Reading))
};
struct FleetPolicy final {
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
struct FleetState final:S::TransmissibleState<FleetState,FleetValue> {
    static constexpr S::StateTypeId TypeId{0x1501};
    static constexpr std::string_view CanonicalName="Example.State.SubscribeAny";
    using ConvergencePolicy=FleetPolicy;
};

static System::DeviceRuntimeIdentity Identity(std::uint8_t marker) {
    System::DeviceIdentifier::Storage bytes{};bytes[0]=marker;
    return {System::DeviceIdentifier{bytes},System::RuntimeIncarnationId{1}};
}

struct Adapter final {
    std::array<System::DeviceIdentifier,2> Owners{Identity(2).Device,Identity(3).Device};
    bool Validate(const S::StateTransportContract&) noexcept { return true; }
    S::StateTransportAdmission Admit(const S::StateOutboundMessage<FleetState>&) noexcept {
        return {S::StateTransportAdmissionStatus::Accepted};
    }
    void Wake() noexcept {}
    S::StateOwnerDiscoveryStatus DiscoverOwners(S::StateOwnerDiscoverySink sink) noexcept {
        for(const auto& owner:Owners)
            if(!sink.TryOffer(owner)) return S::StateOwnerDiscoveryStatus::CapacityUnavailable;
        return S::StateOwnerDiscoveryStatus::Success;
    }
};

Primitive::TypeDirectory<1> directory;
S::Runtime<S::TypeConfiguration<FleetState,S::MaximumRemoteOwners<2>,S::MaximumSubscribers<0>>> states;
S::StateTransportBinding<FleetState,Serializable::DirectBinary> binding;
Adapter adapter;

void setup() {
    assert(System::RuntimeIdentity::Install(Identity(1))==System::RuntimeIdentity::InstallationStatus::Success);
    (void)directory.Register<FleetState>();
    (void)directory.Initialize();
    assert((binding.InitializeWithDiscovery<Adapter,&Adapter::Admit,&Adapter::Validate,&Adapter::Wake,&Adapter::DiscoverOwners>(adapter)));
    assert(states.BindTransport(binding)==S::StateRuntimeStatus::Success);
    assert(states.Initialize(directory.View())==S::StateRuntimeStatus::Success);
    assert(states.Start()==S::StateRuntimeStatus::Success);

    const auto subscriptions=states.SubscribeAny<FleetState>();
    assert(subscriptions && subscriptions.SessionCount==2);
    // AnyDevice is local expansion: two normal concrete SubscribeRequest sessions were created.
}

void loop() {}
