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
    std::uint32_t Reading=0;
    constexpr bool operator==(const RemoteValue& other) const noexcept { return Reading==other.Reading; }
    ESPRESSIO_SERIALIZABLE_TYPE(RemoteValue)
    ESPRESSIO_SERIALIZABLE_SCHEMA_VERSION(1)
    ESPRESSIO_SERIALIZABLE_PROPERTIES(ESPRESSIO_PROPERTY("reading",Reading))
};
struct RemotePolicy final {
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
struct RemoteState final:S::TransmissibleState<RemoteState,RemoteValue> {
    static constexpr S::StateTypeId TypeId{0x1401};
    static constexpr std::string_view CanonicalName="Example.State.SubscribeFrom";
    using ConvergencePolicy=RemotePolicy;
};

static System::DeviceRuntimeIdentity Identity(std::uint8_t marker) {
    System::DeviceIdentifier::Storage bytes{};bytes[0]=marker;
    return {System::DeviceIdentifier{bytes},System::RuntimeIncarnationId{1}};
}

struct Adapter final {
    bool Validate(const S::StateTransportContract&) noexcept { return true; }
    S::StateTransportAdmission Admit(const S::StateOutboundMessage<RemoteState>&) noexcept {
        return {S::StateTransportAdmissionStatus::Accepted};
    }
    void Wake() noexcept {}
};

Primitive::TypeDirectory<1> directory;
S::Runtime<S::TypeConfiguration<RemoteState,S::MaximumRemoteOwners<1>,S::MaximumSubscribers<1>>> states;
S::StateTransportBinding<RemoteState,Serializable::DirectBinary> binding;
Adapter adapter;

void setup() {
    assert(System::RuntimeIdentity::Install(Identity(1))==System::RuntimeIdentity::InstallationStatus::Success);
    (void)directory.Register<RemoteState>();
    (void)directory.Initialize();
    assert((binding.Initialize<Adapter,&Adapter::Admit,&Adapter::Validate,&Adapter::Wake>(adapter)));
    assert(states.BindTransport(binding)==S::StateRuntimeStatus::Success);
    assert(states.Initialize(directory.View())==S::StateRuntimeStatus::Success);
    assert(states.Start()==S::StateRuntimeStatus::Success);

    const auto subscription=states.SubscribeFrom<RemoteState>(Identity(2).Device);
    assert(subscription); // a concrete SubscribeRequest has been transferred to the adapter.
}

void loop() {}
