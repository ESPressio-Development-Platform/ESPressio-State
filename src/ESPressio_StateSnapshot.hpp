#pragma once
#include <ESPressio_TimeReliability.hpp>
namespace ESPressio::State {
template<class TState>
struct StateSnapshot final {
    using ValueType=typename TState::ValueType;
    ValueType Value{};
    Timing::QualifiedTime TruthTime{};
};
}
