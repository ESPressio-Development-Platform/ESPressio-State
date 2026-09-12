#pragma once
#include <type_traits>
#include <utility>
namespace ESPressio::State {
template<class TState>
struct StateComparison {
    using Value=typename TState::ValueType;
    static constexpr bool Equals(const Value& previous,const Value& candidate) noexcept(noexcept(previous==candidate)) {
        return previous==candidate;
    }
};
namespace Detail {
template<class TState>
constexpr bool ValidateStateComparison() noexcept {
    using V=typename TState::ValueType;
    static_assert(noexcept(StateComparison<TState>::Equals(std::declval<const V&>(),std::declval<const V&>())),
                  "StateComparison<T>::Equals must be noexcept; authoritative Set cannot run fallible application comparison under commit synchronization");
    return true;
}
}
}
