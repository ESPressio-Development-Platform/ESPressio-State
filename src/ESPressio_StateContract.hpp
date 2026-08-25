#pragma once

#include <cstddef>
#include <cstdint>
#include <tuple>
#include <type_traits>

namespace ESPressio {
namespace State {

using StateTypeId = uint64_t;
using StateRevision = uint64_t;
using StateEpoch = uint32_t;

template<typename TDefinition, typename = void>
struct StateDefinitionTraits;

template<typename TDefinition>
struct StateDefinitionTraits<TDefinition, std::void_t<typename TDefinition::Value>> {
    using Definition = TDefinition;
    using Value = typename TDefinition::Value;

    static constexpr StateTypeId Id = TDefinition::Id;
};

template<typename TDefinition>
using StateValueType = typename StateDefinitionTraits<TDefinition>::Value;

template<typename TDefinition>
inline constexpr StateTypeId StateTypeIdOf = StateDefinitionTraits<TDefinition>::Id;

template<typename... TDefinitions>
class StateContract final {
public:
    using Definitions = std::tuple<TDefinitions...>;
    static constexpr std::size_t StateCount = sizeof...(TDefinitions);

    template<typename TDefinition>
    static constexpr bool Contains = (std::is_same_v<TDefinition, TDefinitions> || ...);

    template<typename TDefinition>
    static constexpr std::size_t IndexOf() {
        static_assert(Contains<TDefinition>, "State definition is not part of this StateContract");
        return IndexOfImpl<TDefinition, 0, TDefinitions...>();
    }

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
