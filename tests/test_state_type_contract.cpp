#include <ESPressio_States.hpp>
#include <ESPressio_SerializationMacros.hpp>
#include <ESPressio_TypeDirectory.hpp>
#include <cassert>
#include <string_view>
#include <type_traits>
using namespace ESPressio;
namespace S=ESPressio::State;

struct LocalFlag final : S::State<LocalFlag,bool> {
    static constexpr S::StateTypeId TypeId{1001};
    static constexpr std::string_view CanonicalName="Test.State.LocalFlag";
};
struct SerializableValue final {
    int Value=0;
    constexpr bool operator==(const SerializableValue& other) const noexcept { return Value==other.Value; }
    ESPRESSIO_SERIALIZABLE_TYPE(SerializableValue)
    ESPRESSIO_SERIALIZABLE_SCHEMA_VERSION(1)
    ESPRESSIO_SERIALIZABLE_PROPERTIES(ESPRESSIO_PROPERTY("value",Value))
};
struct SerializableFlag final : S::SerializableState<SerializableFlag,SerializableValue> {
    static constexpr S::StateTypeId TypeId{1002};
    static constexpr std::string_view CanonicalName="Test.State.SerializableFlag";
};
struct StateConvergence final {
    using PolicyCategory=Primitive::StateConvergencePolicyTag;
    using RequiredEvidence=Primitive::DestinationPrimitiveAdmission;
    using Supersession=Primitive::LatestAuthoritativeValue;
    using ExhaustionDisposition=Primitive::DormantNeedsConvergence;
    static constexpr std::uint64_t MaximumResidenceNanoseconds=1000000000ULL;
    static constexpr std::uint64_t MaximumAdapterAdmissionWaitNanoseconds=1000000ULL;
    static constexpr std::uint16_t MaximumAttempts=2;
    static constexpr std::uint64_t MinimumRetrySpacingNanoseconds=1000ULL;
    static constexpr std::uint64_t MaximumRetrySpacingNanoseconds=1000000ULL;
};
struct RemoteFlag final : S::TransmissibleState<RemoteFlag,SerializableValue> {
    static constexpr S::StateTypeId TypeId{1003};
    static constexpr std::string_view CanonicalName="Test.State.RemoteFlag";
    using ConvergencePolicy=StateConvergence;
};

int main(){
    static_assert(std::is_same_v<decltype(LocalFlag::TypeId),const S::StateTypeId>);
    static_assert(bool(LocalFlag::TypeId));
    static_assert(!LocalFlag::IsSerializableState && !LocalFlag::IsTransmissibleState);
    static_assert(SerializableFlag::ValidateTier());
    static_assert(RemoteFlag::ValidateTier());
    static_assert(sizeof(S::StateSessionToken)==4 && sizeof(S::StateResyncToken)==4);

    Primitive::TypeDirectory<3> directory;
    assert(directory.Register<LocalFlag>()==Primitive::TypeDirectoryRegistrationStatus::Success);
    assert(directory.Register<SerializableFlag>()==Primitive::TypeDirectoryRegistrationStatus::Success);
    assert(directory.Register<RemoteFlag>()==Primitive::TypeDirectoryRegistrationStatus::Success);
    assert(directory.Initialize()==Primitive::TypeDirectoryInitializationStatus::Success);

    const auto* localCommon=directory.View().Find({S::StateFamilyId,LocalFlag::TypeId.Value()});
    const auto* serialCommon=directory.View().Find({S::StateFamilyId,SerializableFlag::TypeId.Value()});
    const auto* remoteCommon=directory.View().Find({S::StateFamilyId,RemoteFlag::TypeId.Value()});
    assert(localCommon && serialCommon && remoteCommon);
    const auto* local=S::GetStateTypeDescriptor(*localCommon);
    const auto* serial=S::GetStateTypeDescriptor(*serialCommon);
    const auto* remote=S::GetStateTypeDescriptor(*remoteCommon);
    assert(local && local->Tier==S::StateTier::Local && !local->ValueSchema && !local->ConvergencePolicy);
    assert(serial && serial->Tier==S::StateTier::Serializable && serial->ValueSchema && !serial->ConvergencePolicy);
    assert(remote && remote->Tier==S::StateTier::Transmissible && remote->ValueSchema && remote->ConvergencePolicy);
    assert((serial->MaximumSerializedValueBytes[0]==Serializable::MaximumSerializedSize<SerializableValue,Serializable::DirectBinary>));
    assert(serialCommon->SerializedSize.MaximumCompletePrimitiveWireBytes>=serial->MaximumSerializedValueBytes[0]);
    assert((remote->MaximumPublicationWireBytes[0]==S::MaximumCompleteStatePublicationWireBytes<RemoteFlag,Serializable::DirectBinary>));
    assert((remote->MaximumPublicationWireBytes[1]==S::MaximumCompleteStatePublicationWireBytes<RemoteFlag,Serializable::CBOR>));
    assert((remote->MaximumPublicationWireBytes[2]==S::MaximumCompleteStatePublicationWireBytes<RemoteFlag,Serializable::JSON>));
    assert((remote->MaximumSnapshotControlWireBytes[0]==S::MaximumCompleteStateSnapshotControlWireBytes<RemoteFlag,Serializable::DirectBinary>));
    assert(remote->Resources.RuntimeBytes==sizeof(S::StateTypeRuntime<RemoteFlag>));
    assert(remote->Resources.ValueBytes==sizeof(SerializableValue));
    assert(remoteCommon->SerializedSize.MaximumCompletePrimitiveWireBytes>=remote->Resources.MaximumPublicationWireBytes);
    assert(!(localCommon->Contract==serialCommon->Contract));
    assert(!(serialCommon->Contract==remoteCommon->Contract));
}
