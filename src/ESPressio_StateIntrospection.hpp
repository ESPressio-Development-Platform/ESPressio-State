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

// Optional symbolic metadata. A State definition may expose:
//
//     static constexpr const char* Name = "motor.speed";
//
// Names are diagnostic metadata only and never participate in State identity,
// storage, subscription matching, transport, or ordering.
template<typename TDefinition, typename = void>
struct StateIntrospectionTraits {
    static constexpr const char* Name = nullptr;
};

template<typename TDefinition>
struct StateIntrospectionTraits<
    TDefinition,
    std::void_t<decltype(TDefinition::Name)>
> {
    static_assert(
        std::is_convertible_v<decltype(TDefinition::Name), const char*>,
        "State definition Name must be convertible to const char*"
    );
    static constexpr const char* Name = TDefinition::Name;
};

template<typename TDefinition>
inline constexpr const char* StateNameOf =
    StateIntrospectionTraits<TDefinition>::Name;

template<typename TDefinition>
struct RemoteStateIntrospectionSnapshot final {
    using Definition = TDefinition;
    using Value = StateValueType<TDefinition>;

    DeviceIdentifier Device{};
    StateTypeId TypeId = StateTypeIdOf<TDefinition>;
    const char* Name = StateNameOf<TDefinition>;
    RemoteStateSnapshot<Value> State{};
};

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

    static bool TryGetTypeId(
        const char* name,
        StateTypeId& typeId
    ) {
        typeId = 0;
        return FindTypeIdByName(name, typeId);
    }

    template<typename TCallback>
    static bool Visit(
        StateTypeId typeId,
        TCallback&& callback
    ) {
        return VisitType(typeId, std::forward<TCallback>(callback));
    }

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

    template<std::size_t TMaximumDevices, typename TCallback>
    static void ForEachState(
        const RemoteStateManager<TContract, TMaximumDevices>& manager,
        const DeviceIdentifier& device,
        TCallback&& callback
    ) {
        auto visitor = std::forward<TCallback>(callback);
        VisitDeviceStates(manager, device, visitor);
    }

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
