#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace ESPressio {
namespace State {

class DeviceIdentifier final {
public:
    static constexpr std::size_t Size = 16;
    static constexpr std::size_t MacAddressSize = 6;
    using Storage = std::array<uint8_t, Size>;

private:
    Storage _bytes{};

public:
    constexpr DeviceIdentifier() = default;
    constexpr explicit DeviceIdentifier(const Storage& bytes) : _bytes(bytes) {}

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

    constexpr const Storage& Bytes() const noexcept { return _bytes; }

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

    bool TryGetMacAddress(uint8_t* macAddress) const noexcept {
        if (macAddress == nullptr || !IsMacAddressBacked()) return false;
        std::memcpy(macAddress, _bytes.data() + 10, MacAddressSize);
        return true;
    }

    constexpr bool IsZero() const noexcept {
        for (const auto value : _bytes) {
            if (value != 0) return false;
        }
        return true;
    }

    constexpr bool operator==(const DeviceIdentifier& other) const noexcept {
        for (std::size_t index = 0; index < Size; ++index) {
            if (_bytes[index] != other._bytes[index]) return false;
        }
        return true;
    }

    constexpr bool operator!=(const DeviceIdentifier& other) const noexcept {
        return !(*this == other);
    }

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
