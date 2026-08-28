#pragma once

#ifndef ESPRESSIO_STATE_ENABLE_INTROSPECTION
#define ESPRESSIO_STATE_ENABLE_INTROSPECTION 1
#endif

#if ESPRESSIO_STATE_ENABLE_INTROSPECTION

#include <cstddef>
#include <cstring>
#include <tuple>
#include <type_traits>
#include <utility>

#include "ESPressio_RemoteStateManager.hpp"
#include "ESPressio_StateContract.hpp"

namespace ESPressio {
namespace State {

/// <summary>Provides optional symbolic metadata for a state definition.</summary>
/// <typeparam name="TDefinition">State definition being inspected.</typeparam>
/// <remarks>A definition may expose <c>static constexpr const char* Name</c>. Names are diagnostic metadata only and do not participate in state identity, storage, transport, subscription matching, or ordering.</remarks>
template<typename TDefinition, typename = void>
struct StateIntrospectionTraits {
    /// <summary>Optional human-readable state name, or null when none is declared.</summary>
    static constexpr const char* Name = nullptr;
};

/// <summary>Introspection specialization for state definitions exposing a symbolic <c>Name</c>.</summary>
template<typename TDefinition>
struct StateIntrospectionTraits<
    TDefinition,
    std::void_t<decltype(TDefinition::Name)>
> {
    static_assert(
        std::is_convertible_v<decltype(TDefinition::Name), const char*>,
        "State definition Name must be convertible to const char*"
    );
    /// <summary>Human-readable name declared by the state definition.</summary>
    static constexpr const char* Name = TDefinition::Name;
};

/// <summary>Resolves the optional symbolic name associated with a state definition.</summary>
template<typename TDefinition>
inline constexpr const char* StateNameOf =
    StateIntrospectionTraits<TDefinition>::Name;

/// <summary>Combines typed remote-state data with runtime-identifiable state metadata.</summary>
/// <typeparam name="TDefinition">State definition represented by the snapshot.</typeparam>
template<typename TDefinition>
struct RemoteStateIntrospectionSnapshot final {
    /// <summary>State definition represented by this snapshot.</summary>
    using Definition = TDefinition;
    /// <summary>Value type represented by the state definition.</summary>
    using Value = StateValueType<TDefinition>;

    /// <summary>Remote device owning the state.</summary>
    DeviceIdentifier Device{};
    /// <summary>Stable state type identifier.</summary>
    StateTypeId TypeId = StateTypeIdOf<TDefinition>;
    /// <summary>Optional symbolic state name.</summary>
    const char* Name = StateNameOf<TDefinition>;
    /// <summary>Typed remote-state snapshot.</summary>
    RemoteStateSnapshot<Value> State{};
};

/// <summary>Provides runtime lookup and iteration across the typed definitions in a state contract.</summary>
/// <typeparam name="TContract">State contract whose definitions are exposed for introspection.</typeparam>
template<typename TContract>
class StateIntrospection final {
private:
    template<std::size_t TIndex = 0, typename TCallback>
    static bool VisitType(
        StateTypeId typeId,
        TCallback&& callback
    ) {
        if constexpr (TIndex < TContract::StateCount) {
            using Definition = typename std::tuple_element<
                TIndex,
                typename TContract::Definitions
            >::type;

            if (typeId == StateTypeIdOf<Definition>) {
                callback(StateTag<Definition>{});
                return true;
            }
            return VisitType<TIndex + 1>(
                typeId,
                std::forward<TCallback>(callback)
            );
        }
        return false;
    }

    template<std::size_t TIndex = 0>
    static bool FindTypeIdByName(
        const char* name,
        StateTypeId& typeId
    ) {
        if constexpr (TIndex < TContract::StateCount) {
            using Definition = typename std::tuple_element<
                TIndex,
                typename TContract::Definitions
            >::type;
            const char* candidate = StateNameOf<Definition>;
            if (
                candidate != nullptr &&
                name != nullptr &&
                std::strcmp(candidate, name) == 0
            ) {
                typeId = StateTypeIdOf<Definition>;
                return true;
            }
            return FindTypeIdByName<TIndex + 1>(name, typeId);
        }
        return false;
    }

    template<std::size_t TIndex = 0, typename TManager, typename TCallback>
    static void VisitDeviceStates(
        const TManager& manager,
        const DeviceIdentifier& device,
        TCallback& callback
    ) {
        if constexpr (TIndex < TContract::StateCount) {
            using Definition = typename std::tuple_element<
                TIndex,
                typename TContract::Definitions
            >::type;
            RemoteStateSnapshot<StateValueType<Definition>> snapshot;
            if (
                manager.template Read<Definition>(device, snapshot) &&
                snapshot.HasValue
            ) {
                callback(RemoteStateIntrospectionSnapshot<Definition>{
                    device,
                    StateTypeIdOf<Definition>,
                    StateNameOf<Definition>,
                    snapshot
                });
            }
            VisitDeviceStates<TIndex + 1>(manager, device, callback);
        }
    }

public:
    /// <summary>Attempts to resolve a runtime state type identifier to its optional symbolic name.</summary>
    static bool TryGetName(
        StateTypeId typeId,
        const char*& name
    ) {
        name = nullptr;
        return VisitType(typeId, [&](auto tag) {
            using Definition = typename decltype(tag)::Definition;
            name = StateNameOf<Definition>;
        }) && name != nullptr;
    }

    /// <summary>Attempts to resolve a symbolic state name to its stable type identifier.</summary>
    static bool TryGetTypeId(
        const char* name,
        StateTypeId& typeId
    ) {
        typeId = 0;
        return FindTypeIdByName(name, typeId);
    }

    /// <summary>Visits the strongly typed state definition corresponding to a runtime type identifier.</summary>
    /// <typeparam name="TCallback">Callable accepting a typed <c>StateTag</c>.</typeparam>
    /// <returns><c>true</c> when the type identifier belongs to the contract.</returns>
    template<typename TCallback>
    static bool Visit(
        StateTypeId typeId,
        TCallback&& callback
    ) {
        return VisitType(typeId, std::forward<TCallback>(callback));
    }

    /// <summary>Reads one typed remote state and enriches it with introspection metadata.</summary>
    template<typename TDefinition, std::size_t TMaximumDevices>
    static bool Read(
        const RemoteStateManager<TContract, TMaximumDevices>& manager,
        const DeviceIdentifier& device,
        RemoteStateIntrospectionSnapshot<TDefinition>& output
    ) {
        static_assert(
            TContract::template Contains<TDefinition>,
            "State definition is not part of this StateContract"
        );
        RemoteStateSnapshot<StateValueType<TDefinition>> snapshot;
        if (!manager.template Read<TDefinition>(device, snapshot)) return false;
        output.Device = device;
        output.TypeId = StateTypeIdOf<TDefinition>;
        output.Name = StateNameOf<TDefinition>;
        output.State = snapshot;
        return true;
    }

    /// <summary>Visits each state with a retained value for one remote device.</summary>
    template<std::size_t TMaximumDevices, typename TCallback>
    static void ForEachState(
        const RemoteStateManager<TContract, TMaximumDevices>& manager,
        const DeviceIdentifier& device,
        TCallback&& callback
    ) {
        auto visitor = std::forward<TCallback>(callback);
        VisitDeviceStates(manager, device, visitor);
    }

    /// <summary>Visits each retained state value across all known remote devices.</summary>
    template<std::size_t TMaximumDevices, typename TCallback>
    static void ForEachRemoteState(
        const RemoteStateManager<TContract, TMaximumDevices>& manager,
        TCallback&& callback
    ) {
        auto visitor = std::forward<TCallback>(callback);
        manager.ForEachDevice([&](const RemoteDeviceSnapshot& device) {
            VisitDeviceStates(manager, device.Identifier, visitor);
        });
    }
};

}
}

#endif // ESPRESSIO_STATE_ENABLE_INTROSPECTION
