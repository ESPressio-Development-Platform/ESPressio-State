#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

#include "ESPressio_StateContract.hpp"

namespace ESPressio {
namespace State {

template<typename TDefinition, typename = void>
struct StateCodec {
    using Value = StateValueType<TDefinition>;

    static_assert(
        std::is_trivially_copyable<Value>::value,
        "Default StateCodec requires a trivially-copyable Value; specialize StateCodec<TDefinition> for custom wire representations"
    );

    static constexpr std::size_t MaximumEncodedSize = sizeof(Value);

    static bool Encode(
        const Value& value,
        uint8_t* output,
        std::size_t capacity,
        std::size_t& size
    ) {
        if (output == nullptr || capacity < sizeof(Value)) return false;
        std::memcpy(output, &value, sizeof(Value));
        size = sizeof(Value);
        return true;
    }

    static bool Decode(
        const uint8_t* input,
        std::size_t size,
        Value& value
    ) {
        if (input == nullptr || size != sizeof(Value)) return false;
        std::memcpy(&value, input, sizeof(Value));
        return true;
    }
};

}
}
