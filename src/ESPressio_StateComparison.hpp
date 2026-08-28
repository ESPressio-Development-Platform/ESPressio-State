#pragma once

#include "ESPressio_StateContract.hpp"

namespace ESPressio {
namespace State {

/// <summary>Defines semantic equality for values belonging to one state definition.</summary>
/// <typeparam name="TDefinition">State definition whose meaningful-change semantics are evaluated.</typeparam>
/// <remarks>The default policy delegates to the value type's equality operator. Specialize this policy per state definition to implement tolerance, deadband, hysteresis, noise rejection, or other domain-specific comparison semantics.</remarks>
template<typename TDefinition>
struct StateComparison {
    /// <summary>Value type represented by the state definition.</summary>
    using Value = StateValueType<TDefinition>;

    /// <summary>Determines whether two values are semantically equal for this state definition.</summary>
    static bool Equals(const Value& previous, const Value& current) {
        return previous == current;
    }
};

/// <summary>Determines whether two values are semantically equal using the state definition's comparison policy.</summary>
template<typename TDefinition>
bool StateValuesEqual(
    const StateValueType<TDefinition>& previous,
    const StateValueType<TDefinition>& current
) {
    return StateComparison<TDefinition>::Equals(previous, current);
}

/// <summary>Determines whether a value represents a meaningful change according to the state definition's comparison policy.</summary>
template<typename TDefinition>
bool StateValueChanged(
    const StateValueType<TDefinition>& previous,
    const StateValueType<TDefinition>& current
) {
    return !StateValuesEqual<TDefinition>(previous, current);
}

} // namespace State
} // namespace ESPressio
