#pragma once
#include "ESPressio_SerializableState.hpp"
namespace ESPressio::State {
template<class TDerived,class TValue>
class TransmissibleState : public SerializableState<TDerived,TValue> {
public:
    static constexpr bool IsSerializableState=true;
    static constexpr bool IsTransmissibleState=true;
};
}
