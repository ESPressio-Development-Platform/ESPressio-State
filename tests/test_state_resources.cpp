#include <ESPressio_States.hpp>
#include <ESPressio_SerializationMacros.hpp>
#include <type_traits>
#include <string_view>

using namespace ESPressio;
namespace S=ESPressio::State;

struct ResourceValue final {
    std::uint32_t Reading=0;
    constexpr bool operator==(const ResourceValue& other) const noexcept { return Reading==other.Reading; }
    ESPRESSIO_SERIALIZABLE_TYPE(ResourceValue)
    ESPRESSIO_SERIALIZABLE_SCHEMA_VERSION(1)
    ESPRESSIO_SERIALIZABLE_PROPERTIES(ESPRESSIO_PROPERTY("reading",Reading))
};
struct ResourcePolicy final {
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
struct ResourceState final:S::TransmissibleState<ResourceState,ResourceValue>{
    static constexpr S::StateTypeId TypeId{0x5901};
    static constexpr std::string_view CanonicalName="Test.State.Resources";
    using ConvergencePolicy=ResourcePolicy;
};

using Configuration=S::TypeConfiguration<ResourceState,S::MaximumRemoteOwners<2>,S::MaximumSubscribers<3>>;
using Runtime=S::Runtime<Configuration>;
using Accounting=S::StateRuntimeResourceAccounting<Configuration>;
using ObserverAccounting=S::StateObserverResourceAccounting<ResourceState>;

constexpr auto profile=S::StateDeploymentResources<Configuration>();
static_assert(profile.TypeId==ResourceState::TypeId);
static_assert(profile.PrimitiveDirectoryEntryBytes==sizeof(Primitive::PrimitiveTypeDescriptor));
static_assert(profile.StateDescriptorStaticBytes==sizeof(S::StateTypeDescriptor));
static_assert(profile.ValueBytes==sizeof(ResourceValue));
static_assert(profile.SnapshotBytes==sizeof(S::StateSnapshot<ResourceState>));
static_assert(profile.TypeRuntimeBytes==sizeof(S::StateTypeRuntime<ResourceState>));
static_assert(profile.ObserverRelationBytes==sizeof(S::StateObserverTargetNode));
static_assert(profile.RemoteOwnerCapacity==2);
static_assert(profile.RemoteOwnerSlotBytes==sizeof(S::StateRemoteOwnerSlot<ResourceState>));
static_assert(profile.RemoteOwnerReservedBytes==2*sizeof(S::StateRemoteOwnerSlot<ResourceState>));
static_assert(profile.SubscriberCapacity==3);
static_assert(profile.SubscriberSlotBytes==sizeof(S::StateSourceSubscriberSlot<ResourceState>));
static_assert(profile.SubscriberReservedBytes==3*sizeof(S::StateSourceSubscriberSlot<ResourceState>));
static_assert(profile.RemoteReplicaTableBytes==sizeof(S::StateRemoteReplicaTable<ResourceState,2,3>));
static_assert(profile.OptionalDirectBinaryPersistenceBindingBytes==sizeof(S::StatePersistenceBinding<ResourceState,Serializable::DirectBinary>));
static_assert(profile.OptionalDirectBinaryPersistenceRecordMaximumBytes==S::StatePersistenceBinding<ResourceState,Serializable::DirectBinary>::MaximumRecordBytes);
static_assert(profile.OptionalDirectBinaryTransportBindingBytes==sizeof(S::StateTransportBinding<ResourceState,Serializable::DirectBinary>));
static_assert(Accounting::TypeCount==1);
static_assert(Accounting::RuntimeObjectBytes==sizeof(Runtime));
static_assert(Accounting::StateDirectoryReservedEntryBytes==sizeof(Primitive::PrimitiveTypeDescriptor));
static_assert(Accounting::ProcessTokenAuthorityStaticBytes>=sizeof(S::StateSessionTokenGenerator)+sizeof(S::StateResyncTokenGenerator));
static_assert(Accounting::Types[0].RemoteOwnerCapacity==2);
static_assert(ObserverAccounting::ObjectBytes==sizeof(S::ObserverCapability<ResourceState>));
static_assert(ObserverAccounting::ObservationCount==1);
static_assert(ObserverAccounting::RegistrationNodeBytes==sizeof(S::StateObserverTargetNode));
static_assert(ObserverAccounting::PendingBitmapBytes==sizeof(std::uint64_t));
static_assert(ObserverAccounting::FrameworkStackFloorBytes==S::ObserverCapability<ResourceState>::FrameworkStackFloorBytes);

int main(){}
