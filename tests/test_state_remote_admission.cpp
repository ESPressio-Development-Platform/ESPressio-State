#include <ESPressio_States.hpp>
#include <ESPressio_SerializationMacros.hpp>
#include <array>
#include <cassert>
#include <cstdint>
#include <string_view>
using namespace ESPressio;
namespace S=ESPressio::State;

struct AdmissionValue final {
    std::uint32_t Value=0;
    constexpr bool operator==(const AdmissionValue& other) const noexcept { return Value==other.Value; }
    ESPRESSIO_SERIALIZABLE_TYPE(AdmissionValue)
    ESPRESSIO_SERIALIZABLE_SCHEMA_VERSION(1)
    ESPRESSIO_SERIALIZABLE_PROPERTIES(ESPRESSIO_PROPERTY("value",Value))
};
struct AdmissionPolicy final {
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
struct AdmissionState final : S::TransmissibleState<AdmissionState,AdmissionValue> {
    static constexpr S::StateTypeId TypeId{0x5301};
    static constexpr std::string_view CanonicalName="Test.State.RemoteAdmission";
    using ConvergencePolicy=AdmissionPolicy;
};

static System::DeviceRuntimeIdentity Identity(std::uint8_t marker,std::uint32_t runtime){
    System::DeviceIdentifier::Storage bytes{};bytes[0]=marker;
    return {System::DeviceIdentifier{bytes},System::RuntimeIncarnationId{runtime}};
}

int main(){
    using Format=Serializable::DirectBinary;
    const auto owner=Identity(0x11,10);
    const auto requester=Identity(0x22,20);
    const auto relay=Identity(0x33,30);
    const S::StateSessionToken session{7};

    std::array<std::uint8_t,S::MaximumCompleteStatePublicationWireBytes<AdmissionState,Format>> publication{};
    const S::StateSnapshot<AdmissionState> snapshot{{42},{1234,Timing::TimeReliability::Synchronized}};
    const auto encoded=S::EncodeStatePublication<AdmissionState,Format>(snapshot,{false,5},owner,requester,session,
                                                                        publication.data(),publication.size());
    assert(encoded && encoded.Bytes> S::StatePublicationWireHeaderSize);

    S::StateDecodedIngress<AdmissionState> decoded{};
    auto accepted=S::DecodeValidatedStateIngress<AdmissionState,Format>(publication.data(),encoded.Bytes,{owner},decoded);
    assert(accepted);
    assert(decoded.Kind==S::StateMessageKind::Publication);
    assert(decoded.Owner==owner && decoded.Requester==requester && decoded.Session==session);
    assert((decoded.Version==S::StateVersion{false,5}));
    assert(decoded.HasSnapshot && decoded.Snapshot.Value.Value==42);
    assert(decoded.Snapshot.TruthTime.Nanoseconds==1234);

    // The immediate relay and the requester cannot substitute for the encoded semantic owner.
    auto relayRejected=S::DecodeValidatedStateIngress<AdmissionState,Format>(publication.data(),encoded.Bytes,{relay},decoded);
    assert(!relayRejected && relayRejected.Disposition==Primitive::PrimitiveAdmissionDisposition::Rejected);
    auto requesterRejected=S::DecodeValidatedStateIngress<AdmissionState,Format>(publication.data(),encoded.Bytes,{requester},decoded);
    assert(!requesterRejected && requesterRejected.Disposition==Primitive::PrimitiveAdmissionDisposition::Rejected);
    auto unvalidated=S::DecodeValidatedStateIngress<AdmissionState,Format>(publication.data(),encoded.Bytes,{},decoded);
    assert(!unvalidated && unvalidated.Disposition==Primitive::PrimitiveAdmissionDisposition::Rejected);

    // Requester-originated controls require the validated requester, not owner or relay identity.
    S::StateControlWireHeader request{};
    request.Kind=S::StateMessageKind::SubscribeRequest;
    request.TypeId=AdmissionState::TypeId;
    request.Owner=owner;
    request.Requester=requester;
    request.Session=session;
    std::array<std::uint8_t,S::StateControlWireHeaderSize> control{};
    const auto controlEncoded=S::EncodeStateControl(request,control.data(),control.size());
    assert(controlEncoded && controlEncoded.Bytes==S::StateControlWireHeaderSize);
    accepted=S::DecodeValidatedStateIngress<AdmissionState,Format>(control.data(),control.size(),{requester},decoded);
    assert(accepted && decoded.Kind==S::StateMessageKind::SubscribeRequest && !decoded.HasSnapshot);
    assert(decoded.Owner==owner && decoded.Requester==requester);
    const auto wrongRequesterOwner=S::DecodeValidatedStateIngress<AdmissionState,Format>(control.data(),control.size(),{owner},decoded);
    assert(!wrongRequesterOwner);
    const auto wrongRequesterRelay=S::DecodeValidatedStateIngress<AdmissionState,Format>(control.data(),control.size(),{relay},decoded);
    assert(!wrongRequesterRelay);

    // Type mismatch is unsupported rather than a mutation-capable decode.
    publication[5]^=0x01;
    const auto wrongType=S::DecodeValidatedStateIngress<AdmissionState,Format>(publication.data(),encoded.Bytes,{owner},decoded);
    assert(!wrongType && wrongType.Disposition==Primitive::PrimitiveAdmissionDisposition::Unsupported);
}
