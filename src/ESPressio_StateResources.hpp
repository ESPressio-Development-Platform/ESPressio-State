#pragma once
#include <array>
#include <cstddef>
#include <type_traits>
#include <ESPressio_PrimitiveTypeDescriptor.hpp>
#include "ESPressio_StateDescriptor.hpp"
#include "ESPressio_StateObserverCapability.hpp"
#include "ESPressio_StatePersistence.hpp"
#include "ESPressio_StateRemoteReplica.hpp"
#include "ESPressio_StateRuntime.hpp"
#include "ESPressio_StateTransportBinding.hpp"

namespace ESPressio::State {

/// <summary>Compile-time object footprints for one frozen State deployment configuration.</summary>
/// <remarks>Every value is a target-specific <c>sizeof</c> or an explicit capacity multiplication.
/// Component values intentionally overlap with aggregate object sizes and therefore must not be
/// summed unless the field name explicitly says Reserved/Total. No heap allowance is implied.</remarks>
struct StateDeploymentTypeResourceProfile final {
    StateTypeId TypeId{};
    std::size_t PrimitiveDirectoryEntryBytes=0;
    std::size_t StateDescriptorStaticBytes=0;
    std::size_t ValueBytes=0;
    std::size_t SnapshotBytes=0;
    std::size_t TypeRuntimeBytes=0;
    std::size_t ObserverRelationBytes=0;
    std::size_t RemoteOwnerCapacity=0;
    std::size_t RemoteOwnerSlotBytes=0;
    std::size_t RemoteOwnerReservedBytes=0;
    std::size_t SubscriberCapacity=0;
    std::size_t SubscriberSlotBytes=0;
    std::size_t SubscriberReservedBytes=0;
    std::size_t RemoteReplicaTableBytes=0;
    std::size_t SelectorStateBytes=0;
    std::size_t OptionalDirectBinaryPersistenceBindingBytes=0;
    std::size_t OptionalDirectBinaryPersistenceRecordMaximumBytes=0;
    std::size_t OptionalDirectBinaryTransportBindingBytes=0;
};

namespace Detail {
template<class TState,bool Serializable=TState::IsSerializableState>
struct StatePersistenceResourceFacts final {
    static constexpr std::size_t BindingBytes=0;
    static constexpr std::size_t MaximumRecordBytes=0;
};
template<class TState>
struct StatePersistenceResourceFacts<TState,true> final {
    using Binding=StatePersistenceBinding<TState,Serializable::DirectBinary>;
    static constexpr std::size_t BindingBytes=sizeof(Binding);
    static constexpr std::size_t MaximumRecordBytes=Binding::MaximumRecordBytes;
};

template<class TState,bool Transmissible=TState::IsTransmissibleState>
struct StateTransportResourceFacts final { static constexpr std::size_t BindingBytes=0; };
template<class TState>
struct StateTransportResourceFacts<TState,true> final {
    static constexpr std::size_t BindingBytes=sizeof(StateTransportBinding<TState,Serializable::DirectBinary>);
};
}

template<class TConfiguration>
constexpr StateDeploymentTypeResourceProfile StateDeploymentResources() noexcept {
    using TState=typename TConfiguration::StateType;
    using Table=StateRemoteReplicaTable<TState,TConfiguration::RemoteOwnerCapacity,TConfiguration::SubscriberCapacity>;
    using Selector=Detail::StateSelectorState<TConfiguration>;
    return {
        TState::TypeId,
        sizeof(Primitive::PrimitiveTypeDescriptor),
        sizeof(StateTypeDescriptor),
        sizeof(typename TState::ValueType),
        sizeof(StateSnapshot<TState>),
        sizeof(StateTypeRuntime<TState>),
        sizeof(StateObserverTargetNode),
        TConfiguration::RemoteOwnerCapacity,
        sizeof(StateRemoteOwnerSlot<TState>),
        TConfiguration::RemoteOwnerCapacity*sizeof(StateRemoteOwnerSlot<TState>),
        TConfiguration::SubscriberCapacity,
        sizeof(StateSourceSubscriberSlot<TState>),
        TConfiguration::SubscriberCapacity*sizeof(StateSourceSubscriberSlot<TState>),
        sizeof(Table),
        sizeof(Selector),
        Detail::StatePersistenceResourceFacts<TState>::BindingBytes,
        Detail::StatePersistenceResourceFacts<TState>::MaximumRecordBytes,
        Detail::StateTransportResourceFacts<TState>::BindingBytes
    };
}

/// <summary>Deployment-wide fixed State storage facts for one Runtime Type pack.</summary>
/// <remarks><c>RuntimeObjectBytes</c> already includes its remote tables, selector tables and locks.
/// <c>StateDirectoryReservedEntryBytes</c> covers only the State entries in a caller-owned P1
/// TypeDirectory; the directory's own count/frozen overhead and other primitive families are outside State.
/// Process-token bytes are process-wide static objects and are not part of each Runtime instance.</remarks>
template<class... TConfigurations>
struct StateRuntimeResourceAccounting final {
    using RuntimeType=Runtime<TConfigurations...>;
    static constexpr std::size_t TypeCount=sizeof...(TConfigurations);
    static constexpr std::size_t RuntimeObjectBytes=sizeof(RuntimeType);
    static constexpr std::size_t StateDirectoryReservedEntryBytes=TypeCount*sizeof(Primitive::PrimitiveTypeDescriptor);
    static constexpr std::size_t ProcessTokenAuthorityStaticBytes=
        sizeof(System::Synchronization::Mutex)+sizeof(StateSessionTokenGenerator)+sizeof(StateResyncTokenGenerator);
    inline static constexpr std::array<StateDeploymentTypeResourceProfile,TypeCount> Types{
        StateDeploymentResources<TConfigurations>()...
    };
};

/// <summary>Fixed storage and stack-floor facts for one TH10 State observer capability.</summary>
template<class... TStates>
struct StateObserverResourceAccounting final {
    using Capability=ObserverCapability<TStates...>;
    static constexpr std::size_t ObjectBytes=sizeof(Capability);
    static constexpr std::size_t ObservationCount=Capability::ObservationCount;
    static constexpr std::size_t RegistrationNodeBytes=ObservationCount*sizeof(StateObserverTargetNode);
    static constexpr std::size_t PendingBitmapBytes=Capability::PendingBitmapBytes;
    static constexpr std::uint32_t FrameworkStackFloorBytes=Capability::FrameworkStackFloorBytes;
    static constexpr std::size_t ExternalStorageBytes=Capability::ExternalStorageBytes;
};

} // namespace ESPressio::State
