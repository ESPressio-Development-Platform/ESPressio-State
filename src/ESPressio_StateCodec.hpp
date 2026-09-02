#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

#include "ESPressio_StateContract.hpp"

namespace ESPressio {
namespace State {

/// <summary>Encodes and decodes the wire representation for a state definition.</summary>
/// <typeparam name="TDefinition">State definition whose value representation is encoded.</typeparam>
/// <remarks>The default codec performs an exact byte copy and therefore requires a trivially copyable value type.</remarks>
template<typename TDefinition, typename = void>
struct StateCodec {
    /// <summary>Value type represented by the state definition.</summary>
    using Value = StateValueType<TDefinition>;

    static_assert(
        std::is_trivially_copyable<Value>::value,
        "Default StateCodec requires a trivially-copyable Value; specialize StateCodec<TDefinition> for custom wire representations"
    );

    /// <summary>Maximum number of bytes produced by the default encoder.</summary>
    static constexpr std::size_t MaximumEncodedSize = sizeof(Value);

    /// <summary>Encodes a state value into the supplied byte buffer.</summary>
    /// <param name="value">Value to encode.</param>
    /// <param name="output">Destination byte buffer.</param>
    /// <param name="capacity">Capacity of the destination buffer in bytes.</param>
    /// <param name="size">Receives the encoded byte count.</param>
    /// <returns><c>true</c> when encoding succeeds.</returns>
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

    /// <summary>Decodes a state value from its byte representation.</summary>
    /// <param name="input">Encoded input bytes.</param>
    /// <param name="size">Number of encoded input bytes.</param>
    /// <param name="value">Receives the decoded state value.</param>
    /// <returns><c>true</c> when decoding succeeds.</returns>
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
