#pragma once
#include "ESPressio_State.hpp"
namespace ESPressio::State {
template<class TDerived,class TValue>
class SerializableState : public State<TDerived,TValue> {
public:
    static constexpr bool IsSerializableState=true;
    static constexpr bool IsTransmissibleState=false;
};
}
