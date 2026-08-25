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

        // Preserve the 48-bit MAC address exactly while reserving an explicit
        // namespace marker inside the common 128-bit identifier. The remaining
        // bytes are zero so this mapping is deterministic and reversible.
        bytes[0] = 'M';
        bytes[1] = 'A';
        bytes[2] = 'C';
        bytes[3] = 1;
        std::memcpy(bytes.data() + 10, macAddress, 6);
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
