#pragma once
#include <ESPressio_SerializationTraits.hpp>
#include "ESPressio_State.hpp"
namespace ESPressio::State {
/// <summary>Local State semantics plus a bounded P3 schema for immutable value/snapshot serialization.</summary>
template<class TDerived,class TValue>
class SerializableState : public State<TDerived,TValue> {
public:
    static constexpr bool IsSerializableState=true;
    static constexpr bool IsTransmissibleState=false;
    static constexpr bool ValidateTier() noexcept {
        static_assert(Serializable::IsBoundedSerializable<TValue>,
                      "Serializable State Value requires an explicit nonzero P3 schema version and bounded property graph");
        static_assert(std::is_nothrow_move_assignable_v<TValue>,
                      "Serializable State bounded decode publication requires nonthrowing move assignment");
        return true;
    }
};
}
