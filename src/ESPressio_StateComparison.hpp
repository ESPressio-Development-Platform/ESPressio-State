#pragma once

#include "ESPressio_StateContract.hpp"

namespace ESPressio {
namespace State {

// Defines semantic equality for one State definition.
//
// The default policy delegates to the State Value type's operator==. Consumers
// may specialize StateComparison<TDefinition> to define deadband, tolerance,
// noise rejection, hysteresis or other domain-specific meaningful-change
// semantics. The policy is keyed by State definition rather than Value type so
// two State definitions sharing the same Value may still compare differently.
template<typename TDefinition>
struct StateComparison {
    using Value = StateValueType<TDefinition>;

    static bool Equals(const Value& previous, const Value& current) {
        return previous == current;
    }
};

template<typename TDefinition>
bool StateValuesEqual(
    const StateValueType<TDefinition>& previous,
    const StateValueType<TDefinition>& current
) {
    return StateComparison<TDefinition>::Equals(previous, current);
}

template<typename TDefinition>
bool StateValueChanged(
    const StateValueType<TDefinition>& previous,
    const StateValueType<TDefinition>& current
) {
    return !StateValuesEqual<TDefinition>(previous, current);
}

} // namespace State
} // namespace ESPressio
