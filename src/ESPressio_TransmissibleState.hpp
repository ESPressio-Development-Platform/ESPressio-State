#pragma once
#include <ESPressio_PrimitivePolicy.hpp>
#include "ESPressio_SerializableState.hpp"
namespace ESPressio::State {
/// <summary>Distributed latest-truth State tier with bounded P3 value schema and P2 convergence policy.</summary>
template<class TDerived,class TValue>
class TransmissibleState : public SerializableState<TDerived,TValue> {
public:
    static constexpr bool IsSerializableState=true;
    static constexpr bool IsTransmissibleState=true;
    static constexpr bool ValidateTier() noexcept {
        static_assert(SerializableState<TDerived,TValue>::ValidateTier());
        static_assert(Detail::HasConvergencePolicy<TDerived>::value,
                      "Transmissible State requires a ConvergencePolicy Type");
        static_assert(Primitive::IsStateConvergencePolicy<typename TDerived::ConvergencePolicy>::value,
                      "Transmissible State requires a finite P2 State convergence policy");
        return true;
    }
};
}
