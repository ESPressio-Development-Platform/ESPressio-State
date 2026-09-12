#pragma once
#include <ESPressio_DeviceIdentifier.hpp>
#include "ESPressio_StateTypes.hpp"
namespace ESPressio::State {
struct StateAddress final {
    System::DeviceIdentifier OwnerDeviceIdentifier{};
    StateTypeId TypeId{};
    explicit operator bool() const noexcept { return bool(OwnerDeviceIdentifier) && bool(TypeId); }
    bool operator==(const StateAddress& other) const noexcept {
        return OwnerDeviceIdentifier==other.OwnerDeviceIdentifier && TypeId==other.TypeId;
    }
    bool operator!=(const StateAddress& other) const noexcept { return !(*this==other); }
    bool operator<(const StateAddress& other) const noexcept {
        return OwnerDeviceIdentifier<other.OwnerDeviceIdentifier ||
            (!(other.OwnerDeviceIdentifier<OwnerDeviceIdentifier) && TypeId<other.TypeId);
    }
};
template<class TState>
StateAddress MakeStateAddress(const System::DeviceIdentifier& owner) noexcept { return {owner,TState::TypeId}; }
}
