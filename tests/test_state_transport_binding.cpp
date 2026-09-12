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

struct TransportValue final {
    std::uint32_t Value=0;
    constexpr bool operator==(const TransportValue& other) const noexcept { return Value==other.Value; }
    ESPRESSIO_SERIALIZABLE_TYPE(TransportValue)
    ESPRESSIO_SERIALIZABLE_SCHEMA_VERSION(1)
    ESPRESSIO_SERIALIZABLE_PROPERTIES(ESPRESSIO_PROPERTY("value",Value))
};
struct TransportPolicy final {
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
struct TransportState final : S::TransmissibleState<TransportState,TransportValue> {
    static constexpr S::StateTypeId TypeId{0x5401};
    static constexpr std::string_view CanonicalName="Test.State.TransportBinding";
    using ConvergencePolicy=TransportPolicy;
};

static System::DeviceRuntimeIdentity Identity(std::uint8_t marker,std::uint32_t runtime){
    System::DeviceIdentifier::Storage bytes{};bytes[0]=marker;
    return {System::DeviceIdentifier{bytes},System::RuntimeIncarnationId{runtime}};
}
static Timing::QualifiedTime Capture(){return {77,Timing::TimeReliability::Synchronized};}

struct Adapter final {
    bool Valid=true;
    unsigned Validations=0;
    unsigned Admissions=0;
    unsigned Discoveries=0;
    S::StateTransportContract Contract{};
    S::StateOutboundMessage<TransportState> Last{};
    std::array<System::DeviceIdentifier,2> Owners{Identity(0x31,31).Device,Identity(0x32,32).Device};

    bool Validate(const S::StateTransportContract& contract) noexcept {
        ++Validations;Contract=contract;return Valid;
    }
    S::StateTransportAdmission Admit(const S::StateOutboundMessage<TransportState>& message) noexcept {
        ++Admissions;Last=message;return {S::StateTransportAdmissionStatus::Accepted};
    }
    S::StateOwnerDiscoveryStatus Discover(S::StateOwnerDiscoverySink sink) noexcept {
        ++Discoveries;
        if(!sink) return S::StateOwnerDiscoveryStatus::CapacityUnavailable;
        for(const auto& owner:Owners) if(!sink.TryOffer(owner)) return S::StateOwnerDiscoveryStatus::CapacityUnavailable;
        return S::StateOwnerDiscoveryStatus::Success;
    }
};

struct DiscoveryCapture final {
    std::array<System::DeviceIdentifier,2> Owners{};
    std::size_t Count=0;
    static bool Offer(void* context,const System::DeviceIdentifier& owner) noexcept {
        auto& self=*static_cast<DiscoveryCapture*>(context);
        if(self.Count==self.Owners.size()) return false;
        self.Owners[self.Count++]=owner;return true;
    }
};

int main(){
    assert(System::RuntimeIdentity::Install(Identity(1,1))==System::RuntimeIdentity::InstallationStatus::Success);
    Primitive::TypeDirectory<1> directory;
    assert(directory.Register<TransportState>()==Primitive::TypeDirectoryRegistrationStatus::Success);
    assert(directory.Initialize()==Primitive::TypeDirectoryInitializationStatus::Success);

    Adapter adapter;
    S::StateTransportBinding<TransportState,Serializable::CBOR> binding;
    assert((binding.InitializeWithDiscovery<Adapter,&Adapter::Admit,&Adapter::Validate,&Adapter::Discover>(adapter)));
    using Runtime=S::Runtime<S::TypeConfiguration<TransportState,S::MaximumRemoteOwners<2>,S::MaximumSubscribers<2>>>;
    Runtime runtime;
    auto owner=runtime.BindOwner<TransportState>();assert(owner);
    assert(runtime.BindTransport(binding)==S::StateRuntimeStatus::Success);
    assert(runtime.Initialize(directory.View(),&Capture)==S::StateRuntimeStatus::Success);
    assert(adapter.Validations==0);
    assert(runtime.Start()==S::StateRuntimeStatus::Success);
    assert(adapter.Validations==1);

    const auto& contract=adapter.Contract;
    assert(contract.TypeId==TransportState::TypeId);
    assert(contract.Format==S::StatePayloadFormat::CBOR);
    assert(contract.MaximumPublicationWireBytes==S::MaximumCompleteStatePublicationWireBytes<TransportState,Serializable::CBOR>);
    assert(contract.MaximumControlWireBytes==S::StateControlWireHeaderSize);
    assert(contract.MaximumSnapshotControlWireBytes==S::MaximumCompleteStateSnapshotControlWireBytes<TransportState,Serializable::CBOR>);
    assert(contract.MaximumAcceptanceControlWireBytes==S::StateAcceptanceControlWireHeaderSize);
    assert(contract.ConvergencePolicy!=nullptr && contract.SupportsOwnerDiscovery);

    S::StateOutboundMessage<TransportState> message{};
    message.Kind=S::StateMessageKind::Publication;
    message.Owner=Identity(1,1);message.Requester=Identity(0x41,41);
    message.Session=S::StateSessionToken{9};message.Version={false,1};
    message.Snapshot={{123},{456,Timing::TimeReliability::Synchronized}};message.HasSnapshot=true;
    const auto admitted=S::StateTypeRuntime<TransportState>::Get().AdmitOutbound(message);
    assert(admitted && adapter.Admissions==1 && adapter.Last.Snapshot.Value.Value==123);

    DiscoveryCapture capture;
    const auto discovery=S::StateTypeRuntime<TransportState>::Get().DiscoverOwners({&capture,&DiscoveryCapture::Offer});
    assert(discovery==S::StateOwnerDiscoveryStatus::Success && adapter.Discoveries==1 && capture.Count==2);
    assert(capture.Owners[0]==adapter.Owners[0] && capture.Owners[1]==adapter.Owners[1]);

    Adapter secondAdapter;
    S::StateTransportBinding<TransportState,Serializable::DirectBinary> secondBinding;
    assert((secondBinding.Initialize<Adapter,&Adapter::Admit,&Adapter::Validate>(secondAdapter)));
    assert(runtime.BindTransport(secondBinding)==S::StateRuntimeStatus::Frozen);
    assert(runtime.Shutdown()==S::StateRuntimeStatus::Success);
}
