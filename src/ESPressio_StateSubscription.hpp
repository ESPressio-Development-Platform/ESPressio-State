#pragma once

#include <array>
#include <cstddef>

#include "ESPressio_DeviceIdentifier.hpp"
#include "ESPressio_StateContract.hpp"

namespace ESPressio {
namespace State {

enum class StateSubscriptionScope : uint8_t {
    AnyDevice = 0,
    SpecificDevice
};

template<typename TDefinition>
struct StateSubscription final {
    static constexpr StateTypeId TypeId = StateTypeIdOf<TDefinition>;

    StateSubscriptionScope Scope = StateSubscriptionScope::AnyDevice;
    DeviceIdentifier Device{};

    static constexpr StateSubscription Any() {
        return StateSubscription{};
    }

    static constexpr StateSubscription From(const DeviceIdentifier& device) {
        StateSubscription result;
        result.Scope = StateSubscriptionScope::SpecificDevice;
        result.Device = device;
        return result;
    }

    bool Matches(const DeviceIdentifier& device) const noexcept {
        return Scope == StateSubscriptionScope::AnyDevice || Device == device;
    }
};

template<std::size_t TCapacity>
class StateSubscriptionRegistry final {
    struct Record {
        bool Used = false;
        StateTypeId TypeId = 0;
        StateSubscriptionScope Scope = StateSubscriptionScope::AnyDevice;
        DeviceIdentifier Device{};
    };

    std::array<Record, TCapacity> _records{};

public:
    static constexpr std::size_t Capacity = TCapacity;

    template<typename TDefinition>
    bool Subscribe(const StateSubscription<TDefinition>& subscription = StateSubscription<TDefinition>::Any()) {
        for (const auto& record : _records) {
            if (
                record.Used &&
                record.TypeId == StateTypeIdOf<TDefinition> &&
                record.Scope == subscription.Scope &&
                record.Device == subscription.Device
            ) {
                return true;
            }
        }

        for (auto& record : _records) {
            if (!record.Used) {
                record.Used = true;
                record.TypeId = StateTypeIdOf<TDefinition>;
                record.Scope = subscription.Scope;
                record.Device = subscription.Device;
                return true;
            }
        }
        return false;
    }

    template<typename TDefinition>
    bool Unsubscribe(const StateSubscription<TDefinition>& subscription = StateSubscription<TDefinition>::Any()) {
        for (auto& record : _records) {
            if (
                record.Used &&
                record.TypeId == StateTypeIdOf<TDefinition> &&
                record.Scope == subscription.Scope &&
                record.Device == subscription.Device
            ) {
                record = Record{};
                return true;
            }
        }
        return false;
    }

    template<typename TDefinition>
    bool IsSubscribed(const DeviceIdentifier& device) const {
        for (const auto& record : _records) {
            if (
                record.Used &&
                record.TypeId == StateTypeIdOf<TDefinition> &&
                (record.Scope == StateSubscriptionScope::AnyDevice || record.Device == device)
            ) {
                return true;
            }
        }
        return false;
    }

    std::size_t Count() const {
        std::size_t count = 0;
        for (const auto& record : _records) if (record.Used) ++count;
        return count;
    }
};

}
}
