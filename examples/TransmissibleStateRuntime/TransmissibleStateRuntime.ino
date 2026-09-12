#include <ESPressio_States.hpp>
#include <ESPressio_RuntimeIdentity.hpp>
#include <ESPressio_SerializationMacros.hpp>
#include <ESPressio_TypeDirectory.hpp>
#include <cassert>
#include <cstdint>
#include <string_view>

using namespace ESPressio;
namespace S=ESPressio::State;

struct DistributedValue final {
    std::uint32_t Reading=0;
    constexpr bool operator==(const DistributedValue& other) const noexcept { return Reading==other.Reading; }
    ESPRESSIO_SERIALIZABLE_TYPE(DistributedValue)
    ESPRESSIO_SERIALIZABLE_SCHEMA_VERSION(1)
    ESPRESSIO_SERIALIZABLE_PROPERTIES(ESPRESSIO_PROPERTY("reading",Reading))
};
struct DistributedPolicy final {
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
struct DistributedState final:S::TransmissibleState<DistributedState,DistributedValue> {
    static constexpr S::StateTypeId TypeId{0x1301};
    static constexpr std::string_view CanonicalName="Example.State.Distributed";
    using ConvergencePolicy=DistributedPolicy;
};

static System::DeviceRuntimeIdentity LocalIdentity() {
    System::DeviceIdentifier::Storage bytes{};bytes[0]=0x13;
    return {System::DeviceIdentifier{bytes},System::RuntimeIncarnationId{1}};
}

Primitive::TypeDirectory<1> directory;
S::Runtime<S::TypeConfiguration<DistributedState,S::MaximumRemoteOwners<2>,S::MaximumSubscribers<2>>> states;
S::StateOwner<DistributedState> owner;

void setup() {
    assert(System::RuntimeIdentity::Install(LocalIdentity())==System::RuntimeIdentity::InstallationStatus::Success);
    (void)directory.Register<DistributedState>();
    (void)directory.Initialize();
    owner=states.BindOwner<DistributedState>();
    (void)states.Initialize(directory.View());
    (void)states.Start();
    (void)owner.Set({88});
}

void loop() {}
