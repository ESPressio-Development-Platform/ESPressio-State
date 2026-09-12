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

struct Shared final : S::State<Shared,int> {
    static constexpr S::StateTypeId TypeId{0x5a01};
    static constexpr std::string_view CanonicalName="Test.State.SharedInitialization";
};
struct Fresh final : S::State<Fresh,int> {
    static constexpr S::StateTypeId TypeId{0x5a02};
    static constexpr std::string_view CanonicalName="Test.State.FreshInitialization";
};
struct Adapter final {
    unsigned Wakes=0;
    void Wake() noexcept { ++Wakes; }
    bool Validate(const S::StateTransportContract&) noexcept { return true; }
    S::StateTransportAdmission Admit(const S::StateOutboundMessage<RuntimeState>&) noexcept {
        return {S::StateTransportAdmissionStatus::Accepted};
    }
};
static Timing::QualifiedTime Capture(){return {1,Timing::TimeReliability::Synchronized};}
int main(){
    System::DeviceIdentifier::Storage identity{};identity[0]=1;
    assert(System::RuntimeIdentity::Install({System::DeviceIdentifier{identity},System::RuntimeIncarnationId{1}})==System::RuntimeIdentity::InstallationStatus::Success);
    Primitive::TypeDirectory<3> directory;
    assert(directory.Register<Shared>()==Primitive::TypeDirectoryRegistrationStatus::Success);
    assert(directory.Register<Fresh>()==Primitive::TypeDirectoryRegistrationStatus::Success);
    assert(directory.Register<RuntimeState>()==Primitive::TypeDirectoryRegistrationStatus::Success);
    assert(directory.Initialize()==Primitive::TypeDirectoryInitializationStatus::Success);
    S::Runtime<S::TypeConfiguration<Shared>> first;
    auto owner=first.BindOwner<Shared>();assert(owner);
    assert(first.Initialize(directory.View(),&Capture)==S::StateRuntimeStatus::Success);
    {
        S::Runtime<S::TypeConfiguration<Fresh>,S::TypeConfiguration<Shared>> conflicting;
        assert(conflicting.Initialize(directory.View(),&Capture)!=S::StateRuntimeStatus::Success);
        // Failure cannot roll back the Type prepared by the first Runtime.
        assert(first.Start()==S::StateRuntimeStatus::Success);
        assert(owner.Set(1)==S::StateSetStatus::Changed);
    }
    S::Runtime<S::TypeConfiguration<Fresh>> next;
    auto fresh=next.BindOwner<Fresh>();assert(fresh);
    assert(next.Initialize(directory.View(),&Capture)==S::StateRuntimeStatus::Success);
    assert(next.Start()==S::StateRuntimeStatus::Success);
    assert(fresh.Set(2)==S::StateSetStatus::Changed);

    Primitive::TypeDirectory<1> missing;
    assert(missing.Register<Shared>()==Primitive::TypeDirectoryRegistrationStatus::Success);
    assert(missing.Initialize()==Primitive::TypeDirectoryInitializationStatus::Success);
    Adapter adapter;
    S::StateTransportBinding<RuntimeState,Serializable::DirectBinary> binding;
    assert((binding.Initialize<Adapter,&Adapter::Admit,&Adapter::Validate,&Adapter::Wake>(adapter)));
    using RemoteRuntime=S::Runtime<S::TypeConfiguration<RuntimeState,S::MaximumSubscribers<1>>>;
    auto abandoned=std::make_unique<RemoteRuntime>();
    assert(abandoned->BindTransport(binding)==S::StateRuntimeStatus::Success);
    assert(abandoned->Initialize(missing.View(),&Capture)==S::StateRuntimeStatus::InvalidDirectory);
    abandoned.reset();
    // Failed staging must release its own borrowed bindings and Type claim.
    RemoteRuntime replacement;
    assert(replacement.BindTransport(binding)==S::StateRuntimeStatus::Success);
    assert(replacement.Initialize(directory.View(),&Capture)==S::StateRuntimeStatus::Success);
    assert(replacement.Start()==S::StateRuntimeStatus::Success);
    assert(first.Shutdown()==S::StateRuntimeStatus::Success);
    assert(next.Shutdown()==S::StateRuntimeStatus::Success);
    assert(replacement.Shutdown()==S::StateRuntimeStatus::Success);
}
