#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <tuple>
#include <type_traits>

namespace ESPressio {
namespace State {

/// <summary>Stable numeric identifier assigned to a state definition.</summary>
using StateTypeId = uint64_t;

/// <summary>Monotonically increasing revision associated with a state value.</summary>
using StateRevision = uint64_t;

/// <summary>Epoch identifier used to distinguish revision domains across resets or restarts.</summary>
using StateEpoch = uint32_t;

/// <summary>Extracts the value type and stable identifier exposed by a state definition.</summary>
/// <typeparam name="TDefinition">State definition type.</typeparam>
template<typename TDefinition, typename = void>
struct StateDefinitionTraits;

/// <summary>State-definition traits specialization for definitions exposing a nested <c>Value</c> type and <c>Id</c>.</summary>
template<typename TDefinition>
struct StateDefinitionTraits<TDefinition, std::void_t<typename TDefinition::Value>> {
    /// <summary>The original state definition type.</summary>
    using Definition = TDefinition;
    /// <summary>The value type represented by the state definition.</summary>
    using Value = typename TDefinition::Value;
    /// <summary>The stable state type identifier.</summary>
    static constexpr StateTypeId Id = TDefinition::Id;
};

/// <summary>Resolves the value type associated with a state definition.</summary>
template<typename TDefinition>
using StateValueType = typename StateDefinitionTraits<TDefinition>::Value;

/// <summary>Resolves the stable type identifier associated with a state definition.</summary>
template<typename TDefinition>
inline constexpr StateTypeId StateTypeIdOf = StateDefinitionTraits<TDefinition>::Id;

/// <summary>Compile-time tag carrying a state definition, its value type, and its stable identifier.</summary>
template<typename TDefinition>
struct StateTag final {
    using Definition = TDefinition;
    using Value = StateValueType<TDefinition>;
    static constexpr StateTypeId Id = StateTypeIdOf<TDefinition>;
};

/// <summary>Defines the closed set of state definitions supported by a typed state endpoint.</summary>
/// <typeparam name="TDefinitions">State definition types included in the contract.</typeparam>
template<typename... TDefinitions>
class StateContract final {
public:
    /// <summary>Tuple containing all state definition types in declaration order.</summary>
    using Definitions = std::tuple<TDefinitions...>;
    /// <summary>Number of state definitions in the contract.</summary>
    static constexpr std::size_t StateCount = sizeof...(TDefinitions);
    /// <summary>Stable type identifiers in contract declaration order.</summary>
    inline static constexpr std::array<StateTypeId, StateCount> TypeIds = {
        StateTypeIdOf<TDefinitions>...
    };

    /// <summary>Indicates at compile time whether a definition belongs to this contract.</summary>
    template<typename TDefinition>
    static constexpr bool Contains = (std::is_same_v<TDefinition, TDefinitions> || ...);

    /// <summary>Gets the declaration-order index of a state definition.</summary>
    /// <typeparam name="TDefinition">State definition whose index is required.</typeparam>
    /// <returns>The zero-based contract index.</returns>
    template<typename TDefinition>
    static constexpr std::size_t IndexOf() {
        static_assert(Contains<TDefinition>, "State definition is not part of this StateContract");
        return IndexOfImpl<TDefinition, 0, TDefinitions...>();
    }

    /// <summary>Attempts to resolve a stable state type identifier to its contract index.</summary>
    /// <param name="typeId">Stable state type identifier to locate.</param>
    /// <param name="index">Receives the zero-based contract index when found.</param>
    /// <returns><c>true</c> when the identifier belongs to this contract.</returns>
    static bool TryIndexOf(StateTypeId typeId, std::size_t& index) noexcept {
        for (std::size_t candidate = 0; candidate < StateCount; ++candidate) {
            if (TypeIds[candidate] == typeId) {
                index = candidate;
                return true;
            }
        }
        return false;
    }

    /// <summary>Checks whether every state definition in this contract has a unique type identifier.</summary>
    static constexpr bool HasUniqueTypeIds() noexcept {
        for (std::size_t left = 0; left < StateCount; ++left) {
            for (std::size_t right = left + 1; right < StateCount; ++right) {
                if (TypeIds[left] == TypeIds[right]) return false;
            }
        }
        return true;
    }

    static_assert(HasUniqueTypeIds(), "StateContract contains duplicate StateTypeId values");

private:
    template<typename TDefinition, std::size_t Index, typename TCurrent, typename... TRest>
    static constexpr std::size_t IndexOfImpl() {
        if constexpr (std::is_same_v<TDefinition, TCurrent>) {
            return Index;
        } else {
            static_assert(sizeof...(TRest) > 0, "State definition is not part of this StateContract");
            return IndexOfImpl<TDefinition, Index + 1, TRest...>();
        }
    }
};

}
}
