#pragma once
#include <type_traits>
#include <utility>
#include <ESPressio_PrimitiveTypeDescriptor.hpp>
#include "ESPressio_StateTypes.hpp"
namespace ESPressio::State {
template<class T> struct StateDescriptorProvider;

template<class TDerived,class TValue>
class State {
protected:
    State()=delete;
public:
    using Value=TValue;
    using ValueType=TValue;
    static constexpr bool IsSerializableState=false;
    static constexpr bool IsTransmissibleState=false;
    static Primitive::PrimitiveTypeDescriptor GetPrimitiveTypeDescriptor() noexcept { return StateDescriptorProvider<TDerived>::Describe(); }
};
}
