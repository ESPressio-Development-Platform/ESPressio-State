#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace ESPressio {
namespace State {

/// <summary>Transport-neutral 128-bit identifier for a state-producing or state-consuming device.</summary>
class DeviceIdentifier final {
public:
    /// <summary>Identifier width in bytes.</summary>
    static constexpr std::size_t Size = 16;
    /// <summary>IEEE MAC address width in bytes.</summary>
    static constexpr std::size_t MacAddressSize = 6;
    /// <summary>Underlying fixed-size identifier storage.</summary>
    using Storage = std::array<uint8_t, Size>;

private:
    Storage _bytes{};

public:
    /// <summary>Creates the all-zero identifier.</summary>
    constexpr DeviceIdentifier() = default;
    /// <summary>Creates an identifier from its complete 128-bit storage representation.</summary>
    constexpr explicit DeviceIdentifier(const Storage& bytes) : _bytes(bytes) {}

    /// <summary>Creates a namespaced device identifier that losslessly embeds a 48-bit MAC address.</summary>
    /// <param name="macAddress">Pointer to six MAC-address bytes.</param>
    static DeviceIdentifier FromMacAddress(const uint8_t* macAddress) {
        Storage bytes{};
        if (macAddress == nullptr) return DeviceIdentifier(bytes);

        // Preserve the 48-bit MAC exactly inside a transport-neutral 128-bit
        // namespace. Other identity providers may use their own namespace.
        bytes[0] = 'M';
        bytes[1] = 'A';
        bytes[2] = 'C';
        bytes[3] = 1;
        std::memcpy(bytes.data() + 10, macAddress, MacAddressSize);
        return DeviceIdentifier(bytes);
    }

    /// <summary>Gets the complete immutable byte representation of the identifier.</summary>
    constexpr const Storage& Bytes() const noexcept { return _bytes; }

    /// <summary>Indicates whether this identifier uses the ESPressio MAC-address namespace.</summary>
    bool IsMacAddressBacked() const noexcept {
        if (
            _bytes[0] != 'M' || _bytes[1] != 'A' ||
            _bytes[2] != 'C' || _bytes[3] != 1
        ) return false;
        for (std::size_t index = 4; index < 10; ++index) {
            if (_bytes[index] != 0) return false;
        }
        return true;
    }

    /// <summary>Attempts to recover the embedded MAC address from a MAC-backed identifier.</summary>
    /// <param name="macAddress">Destination buffer for six MAC-address bytes.</param>
    /// <returns><c>true</c> when this identifier contains a MAC address and the output was populated.</returns>
    bool TryGetMacAddress(uint8_t* macAddress) const noexcept {
        if (macAddress == nullptr || !IsMacAddressBacked()) return false;
        std::memcpy(macAddress, _bytes.data() + 10, MacAddressSize);
        return true;
    }

    /// <summary>Indicates whether every identifier byte is zero.</summary>
    constexpr bool IsZero() const noexcept {
        for (const auto value : _bytes) {
            if (value != 0) return false;
        }
        return true;
    }

    /// <summary>Compares two identifiers for byte-for-byte equality.</summary>
    constexpr bool operator==(const DeviceIdentifier& other) const noexcept {
        for (std::size_t index = 0; index < Size; ++index) {
            if (_bytes[index] != other._bytes[index]) return false;
        }
        return true;
    }

    /// <summary>Compares two identifiers for inequality.</summary>
    constexpr bool operator!=(const DeviceIdentifier& other) const noexcept {
        return !(*this == other);
    }

    /// <summary>Provides lexicographic ordering suitable for ordered containers.</summary>
    constexpr bool operator<(const DeviceIdentifier& other) const noexcept {
        for (std::size_t index = 0; index < Size; ++index) {
            if (_bytes[index] < other._bytes[index]) return true;
            if (_bytes[index] > other._bytes[index]) return false;
        }
        return false;
    }
};

}
}
