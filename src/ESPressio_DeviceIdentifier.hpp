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
    using Storage = std::array<uint8_t, Size>;

private:
    Storage _bytes{};

public:
    constexpr DeviceIdentifier() = default;
    constexpr explicit DeviceIdentifier(const Storage& bytes) : _bytes(bytes) {}

    static DeviceIdentifier FromMacAddress(const uint8_t* macAddress) {
        Storage bytes{};
        if (macAddress == nullptr) {
            return DeviceIdentifier(bytes);
        }

        // MAC addresses remain recognisable while occupying the common
        // transport-neutral 128-bit identifier representation.
        bytes[10] = 0xFF;
        bytes[11] = 0xFF;
        std::memcpy(bytes.data() + 12, macAddress, 6);
        return DeviceIdentifier(bytes);
    }

    constexpr const Storage& Bytes() const noexcept { return _bytes; }

    constexpr bool IsZero() const noexcept {
        for (const auto value : _bytes) {
            if (value != 0) return false;
        }
        return true;
    }

    constexpr bool operator==(const DeviceIdentifier& other) const noexcept {
        return _bytes == other._bytes;
    }

    constexpr bool operator!=(const DeviceIdentifier& other) const noexcept {
        return !(*this == other);
    }

    constexpr bool operator<(const DeviceIdentifier& other) const noexcept {
        return _bytes < other._bytes;
    }
};

}
}
