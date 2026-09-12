#include <ESPressio_States.hpp>
#include <ESPressio_SerializationMacros.hpp>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string_view>
using namespace ESPressio;
namespace S=ESPressio::State;

struct Convergence final {
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
struct WireValue final {
    int Value=0;
    constexpr bool operator==(const WireValue& other) const noexcept { return Value==other.Value; }
    ESPRESSIO_SERIALIZABLE_TYPE(WireValue)
    ESPRESSIO_SERIALIZABLE_SCHEMA_VERSION(1)
    ESPRESSIO_SERIALIZABLE_PROPERTIES(ESPRESSIO_PROPERTY("value",Value))
};
struct WireState final : S::TransmissibleState<WireState,WireValue> {
    static constexpr S::StateTypeId TypeId{0x0807060504030201ULL};
    static constexpr std::string_view CanonicalName="Test.State.Wire";
    using ConvergencePolicy=Convergence;
};

static void AssertLE(const std::uint8_t* data,std::uint64_t value,std::size_t bytes){
    for(std::size_t i=0;i<bytes;++i){assert(data[i]==static_cast<std::uint8_t>(value));value>>=8;}
}
static System::DeviceRuntimeIdentity Identity(std::uint8_t base,std::uint32_t runtime){
    System::DeviceIdentifier::Storage bytes{};
    for(std::size_t i=0;i<bytes.size();++i) bytes[i]=static_cast<std::uint8_t>(base+i);
    return {System::DeviceIdentifier{bytes},System::RuntimeIncarnationId{runtime}};
}

int main(){
    static_assert(S::StatePublicationWireHeaderSize==73);
    static_assert(S::StateControlWireHeaderSize==62);
    static_assert(S::StateSnapshotControlWireHeaderSize==78);
    static_assert(S::StateAcceptanceControlWireHeaderSize==65);
    static_assert(static_cast<std::uint8_t>(S::StateMessageKind::ResyncRequired)==14);
    static_assert(WireState::ValidateTier());
    static_assert(S::MaximumCompleteStatePublicationWireBytes<WireState,Serializable::DirectBinary> > S::StatePublicationWireHeaderSize);
    static_assert(S::MaximumCompleteStateSnapshotControlWireBytes<WireState,Serializable::DirectBinary> > S::StateSnapshotControlWireHeaderSize);

    const auto owner=Identity(0x20,0x34333231);
    const auto requester=Identity(0x40,0x54535251);
    const S::StateSessionToken session{0x14131211};
    const S::StateResyncToken resync{0x24232221};

    S::StatePublicationWireHeader publication{
        WireState::TypeId,owner,requester,session,{true,0x6655},
        {0x4847464544434241ULL,Timing::TimeReliability::Holdover},3};
    std::array<std::uint8_t,S::StatePublicationWireHeaderSize+3> pub{};
    assert(S::EncodeStatePublicationHeader(publication,pub.data(),pub.size()));
    AssertLE(pub.data()+0,S::StateFamilyId,2);AssertLE(pub.data()+2,S::StateProtocolVersion,2);
    assert(pub[4]==static_cast<std::uint8_t>(S::StateMessageKind::Publication));
    AssertLE(pub.data()+5,WireState::TypeId.Value(),8);
    for(std::size_t i=0;i<16;++i){assert(pub[13+i]==owner.Device.Bytes()[i]);assert(pub[33+i]==requester.Device.Bytes()[i]);}
    AssertLE(pub.data()+29,owner.Incarnation.Value(),4);AssertLE(pub.data()+49,requester.Incarnation.Value(),4);
    AssertLE(pub.data()+53,session.Value(),4);assert(pub[57]==1);AssertLE(pub.data()+58,0x6655,2);
    AssertLE(pub.data()+60,0x4847464544434241ULL,8);assert(pub[68]==static_cast<std::uint8_t>(Timing::TimeReliability::Holdover));
    AssertLE(pub.data()+69,3,4);pub[73]=1;pub[74]=2;pub[75]=3;
    S::StatePublicationWireHeader decodedPublication{};
    assert(S::DecodeStatePublicationHeader(pub.data(),pub.size(),decodedPublication));
    assert(decodedPublication.TypeId==publication.TypeId && decodedPublication.Owner==owner && decodedPublication.Requester==requester);
    assert(decodedPublication.Session==session && decodedPublication.Version==publication.Version && decodedPublication.PayloadLength==3);
    auto badPublication=pub;badPublication[57]=2;assert(!S::DecodeStatePublicationHeader(badPublication.data(),badPublication.size(),decodedPublication));
    badPublication=pub;badPublication[68]=0xff;assert(!S::DecodeStatePublicationHeader(badPublication.data(),badPublication.size(),decodedPublication));
    badPublication=pub;badPublication[53]=0;badPublication[54]=0;badPublication[55]=0;badPublication[56]=0;
    assert(!S::DecodeStatePublicationHeader(badPublication.data(),badPublication.size(),decodedPublication));
    badPublication=pub;badPublication[2]^=1;
    assert(S::DecodeStatePublicationHeader(badPublication.data(),badPublication.size(),decodedPublication).Status==S::StateWireStatus::UnsupportedProtocol);
    assert(S::DecodeStatePublicationHeader(pub.data(),S::StatePublicationWireHeaderSize,decodedPublication).Status==S::StateWireStatus::InvalidLength);

    // Wrapped compact version phase=1/revision=0 is valid; only phase=0/revision=0 is empty/invalid.
    auto wrapped=publication;wrapped.Version={true,0};wrapped.PayloadLength=0;
    std::array<std::uint8_t,S::StatePublicationWireHeaderSize> wrappedBytes{};
    assert(S::EncodeStatePublicationHeader(wrapped,wrappedBytes.data(),wrappedBytes.size()));
    S::StatePublicationWireHeader wrappedDecoded{};assert(S::DecodeStatePublicationHeader(wrappedBytes.data(),wrappedBytes.size(),wrappedDecoded));
    assert((wrappedDecoded.Version==S::StateVersion{true,0}));
    wrapped.Version={false,0};assert(!S::EncodeStatePublicationHeader(wrapped,wrappedBytes.data(),wrappedBytes.size()));

    // SubscribeRequest is the sole canonical control allowed to carry an unknown owner runtime.
    auto unknownOwner=owner;unknownOwner.Incarnation=System::RuntimeIncarnationId{};
    S::StateControlWireHeader subscribe{S::StateMessageKind::SubscribeRequest,WireState::TypeId,unknownOwner,requester,session,{},0};
    std::array<std::uint8_t,S::StateControlWireHeaderSize> control{};
    assert(S::EncodeStateControl(subscribe,control.data(),control.size()));
    AssertLE(control.data()+0,S::StateFamilyId,2);AssertLE(control.data()+5,WireState::TypeId.Value(),8);
    AssertLE(control.data()+29,0,4);AssertLE(control.data()+49,requester.Incarnation.Value(),4);
    AssertLE(control.data()+53,session.Value(),4);AssertLE(control.data()+57,0,4);assert(control[61]==0);
    S::StateControlWireHeader decodedControl{};assert(S::DecodeStateControl(control.data(),control.size(),decodedControl));
    assert(decodedControl.Kind==S::StateMessageKind::SubscribeRequest && !decodedControl.Owner.Incarnation);

    S::StateControlWireHeader snapshotControl{S::StateMessageKind::ResyncSnapshot,WireState::TypeId,owner,requester,session,resync,0};
    S::StateSnapshotControlWireHeader snapshotHeader{snapshotControl,{false,7},{0x1112131415161718ULL,Timing::TimeReliability::Synchronized},2};
    std::array<std::uint8_t,S::StateSnapshotControlWireHeaderSize+2> snapshotBytes{};
    assert(S::EncodeStateSnapshotControlHeader(snapshotHeader,snapshotBytes.data(),snapshotBytes.size()));
    assert(snapshotBytes[4]==static_cast<std::uint8_t>(S::StateMessageKind::ResyncSnapshot));
    AssertLE(snapshotBytes.data()+57,resync.Value(),4);assert(snapshotBytes[62]==0);AssertLE(snapshotBytes.data()+63,7,2);
    AssertLE(snapshotBytes.data()+65,0x1112131415161718ULL,8);assert(snapshotBytes[73]==static_cast<std::uint8_t>(Timing::TimeReliability::Synchronized));
    AssertLE(snapshotBytes.data()+74,2,4);snapshotBytes[78]=0xaa;snapshotBytes[79]=0xbb;
    S::StateSnapshotControlWireHeader decodedSnapshot{};assert(S::DecodeStateSnapshotControlHeader(snapshotBytes.data(),snapshotBytes.size(),decodedSnapshot));
    assert((decodedSnapshot.Control.Resync==resync && decodedSnapshot.Version==S::StateVersion{false,7}));
    auto zeroResync=snapshotBytes;for(std::size_t i=57;i<61;++i) zeroResync[i]=0;
    assert(!S::DecodeStateSnapshotControlHeader(zeroResync.data(),zeroResync.size(),decodedSnapshot));

    S::StateControlWireHeader acceptedControl{S::StateMessageKind::PublicationAccepted,WireState::TypeId,owner,requester,session,{},0};
    S::StateAcceptanceControlWireHeader accepted{acceptedControl,{true,9}};
    std::array<std::uint8_t,S::StateAcceptanceControlWireHeaderSize> acceptedBytes{};
    assert(S::EncodeStateAcceptanceControl(accepted,acceptedBytes.data(),acceptedBytes.size()));
    assert(acceptedBytes[62]==1);AssertLE(acceptedBytes.data()+63,9,2);
    S::StateAcceptanceControlWireHeader decodedAccepted{};assert(S::DecodeStateAcceptanceControl(acceptedBytes.data(),acceptedBytes.size(),decodedAccepted));
    assert(decodedAccepted.Version==accepted.Version);

    // Full bounded payload round trip: fixed State semantic header + P3 Value object only.
    S::StateSnapshot<WireState> source{};source.Value.Value=1234;source.TruthTime={9999,Timing::TimeReliability::Acquiring};
    std::array<std::uint8_t,S::MaximumCompleteStatePublicationWireBytes<WireState,Serializable::DirectBinary>> complete{};
    auto completeEncoded=S::EncodeStatePublication<WireState,Serializable::DirectBinary>(source,{false,1},owner,requester,session,complete.data(),complete.size());
    assert(completeEncoded && completeEncoded.Bytes<=complete.size());
    S::StatePublicationWireHeader completeHeader{};assert(S::DecodeStatePublicationHeader(complete.data(),completeEncoded.Bytes,completeHeader));
    WireValue decodedValue{};
    assert((S::DecodeStatePublicationValue<WireState,Serializable::DirectBinary>(completeHeader,complete.data()+S::StatePublicationWireHeaderSize,completeHeader.PayloadLength,decodedValue)));
    assert(decodedValue.Value==1234);

    std::array<std::uint8_t,S::MaximumCompleteStateSnapshotControlWireBytes<WireState,Serializable::DirectBinary>> completeSnapshot{};
    S::StateControlWireHeader baseline{S::StateMessageKind::BaselineSnapshot,WireState::TypeId,owner,requester,session,{},0};
    auto baselineEncoded=S::EncodeStateSnapshotControl<WireState,Serializable::DirectBinary>(baseline,source,{false,1},completeSnapshot.data(),completeSnapshot.size());
    assert(baselineEncoded);
    S::StateSnapshotControlWireHeader baselineHeader{};assert(S::DecodeStateSnapshotControlHeader(completeSnapshot.data(),baselineEncoded.Bytes,baselineHeader));
    WireValue baselineValue{};assert((S::DecodeStateSnapshotControlValue<WireState,Serializable::DirectBinary>(baselineHeader,completeSnapshot.data()+S::StateSnapshotControlWireHeaderSize,baselineHeader.PayloadLength,baselineValue)));
    assert(baselineValue.Value==1234);
}
