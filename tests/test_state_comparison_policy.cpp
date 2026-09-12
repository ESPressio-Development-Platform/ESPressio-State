#include <ESPressio_States.hpp>
#include <ESPressio_RuntimeIdentity.hpp>
#include <ESPressio_SerializationMacros.hpp>
#include <ESPressio_TypeDirectory.hpp>
#include <cassert>
#include <cstdint>
#include <string_view>

using namespace ESPressio;
namespace S=ESPressio::State;

struct DeadbandValue final {
    std::int32_t Number=0;
    constexpr bool operator==(const DeadbandValue& other) const noexcept { return Number==other.Number; }
    ESPRESSIO_SERIALIZABLE_TYPE(DeadbandValue)
    ESPRESSIO_SERIALIZABLE_SCHEMA_VERSION(1)
    ESPRESSIO_SERIALIZABLE_PROPERTIES(ESPRESSIO_PROPERTY("number",Number))
};
struct DeadbandPolicy final {
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
struct DeadbandState final:S::TransmissibleState<DeadbandState,DeadbandValue>{
    static constexpr S::StateTypeId TypeId{0x5801};
    static constexpr std::string_view CanonicalName="Test.State.Deadband";
    using ConvergencePolicy=DeadbandPolicy;
};

template<>
struct ESPressio::State::StateComparison<DeadbandState> final {
    static constexpr bool Equals(const DeadbandValue& previous,const DeadbandValue& candidate) noexcept {
        const auto delta=previous.Number>candidate.Number ? previous.Number-candidate.Number : candidate.Number-previous.Number;
        return delta<5;
    }
};

static System::DeviceRuntimeIdentity Identity(std::uint8_t marker){
    System::DeviceIdentifier::Storage bytes{};bytes[0]=marker;
    return {System::DeviceIdentifier{bytes},System::RuntimeIncarnationId{1}};
}
static Timing::QualifiedTime Capture(){return {100,Timing::TimeReliability::Synchronized};}

int main(){
    const auto local=Identity(1),remote=Identity(2);
    assert(System::RuntimeIdentity::Install(local)==System::RuntimeIdentity::InstallationStatus::Success);
    Primitive::TypeDirectory<1> directory;
    assert(directory.Register<DeadbandState>()==Primitive::TypeDirectoryRegistrationStatus::Success);
    assert(directory.Initialize()==Primitive::TypeDirectoryInitializationStatus::Success);

    using Runtime=S::Runtime<S::TypeConfiguration<DeadbandState,S::MaximumRemoteOwners<1>,S::MaximumSubscribers<0>>>;
    Runtime runtime;
    auto owner=runtime.BindOwner<DeadbandState>();assert(owner);
    assert(runtime.Initialize(directory.View(),&Capture)==S::StateRuntimeStatus::Success);
    assert(runtime.Start()==S::StateRuntimeStatus::Success);

    // Owner comparison is part of authoritative Set admission. A candidate inside
    // the application-defined deadband is a strict no-op: value, TruthTime and
    // compact version remain unchanged.
    assert(owner.Set({100},{100,Timing::TimeReliability::Synchronized})==S::StateSetStatus::Changed);
    assert((runtime.Version<DeadbandState>()==S::StateVersion{false,1}));
    assert(owner.Set({103},{999,Timing::TimeReliability::Unqualified})==S::StateSetStatus::NoChange);
    S::StateSnapshot<DeadbandState> localRead{};
    assert(runtime.TryRead(localRead));
    assert(localRead.Value.Number==100 && localRead.TruthTime.Nanoseconds==100);
    assert((runtime.Version<DeadbandState>()==S::StateVersion{false,1}));
    assert(owner.Set({110},{110,Timing::TimeReliability::Holdover})==S::StateSetStatus::Changed);
    assert((runtime.Version<DeadbandState>()==S::StateVersion{false,2}));

    // Remote replica admission is version/provenance driven and must not run the
    // local owner's semantic equality/deadband policy. Version 2 with value 103
    // is newer authoritative truth and is accepted even though 100 -> 103 would
    // be a local Set no-op under StateComparison<DeadbandState>.
    const auto session=runtime.ReserveSubscriptionSession<DeadbandState>(remote.Device);assert(session);
    S::StateSnapshot<DeadbandState> first{{100},{200,Timing::TimeReliability::Synchronized}};
    assert(runtime.InstallSubscribeSnapshot<DeadbandState>(remote,session.Handle.Session,{false,1},first)==S::StateRemoteStatus::Success);
    S::StateSnapshot<DeadbandState> second{{103},{201,Timing::TimeReliability::Acquiring}};
    assert(runtime.ApplyRemotePublication<DeadbandState>(remote,session.Handle.Session,{false,2},second)==S::StateRemoteStatus::Success);
    S::StateSnapshot<DeadbandState> remoteRead{};
    assert(runtime.TryReadRemote<DeadbandState>(remote.Device,remoteRead));
    assert(remoteRead.Value.Number==103 && remoteRead.TruthTime.Nanoseconds==201);
    assert(runtime.ApplyRemotePublication<DeadbandState>(remote,session.Handle.Session,{false,2},second)==S::StateRemoteStatus::Duplicate);
    assert(runtime.ApplyRemotePublication<DeadbandState>(remote,session.Handle.Session,{false,1},first)==S::StateRemoteStatus::Older);

    assert(runtime.Shutdown()==S::StateRuntimeStatus::Success);
}
