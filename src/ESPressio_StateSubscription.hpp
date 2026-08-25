#pragma once

#include <array>
#include <cstddef>
#include <memory>

#include <ESPressio_ThreadSafeObservable.hpp>

#include "ESPressio_DeviceIdentifier.hpp"
#include "ESPressio_StateContract.hpp"
#include "ESPressio_StateObservers.hpp"

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
    class RegistryObservable final : public Observable::ThreadSafeObservable {
    public:
        void Subscribed(
            StateTypeId typeId,
            StateSubscriptionScope scope,
            const DeviceIdentifier& device
        ) {
            ExecuteNotification([&](NotificationContext& notification) {
                notification.WithObservers<IStateSubscriptionRegistryObserver>(
                    [&](IStateSubscriptionRegistryObserver* observer) {
                        observer->OnStateSubscribed(typeId, scope, device);
                    }
                );
            });
        }

        void Unsubscribed(
            StateTypeId typeId,
            StateSubscriptionScope scope,
            const DeviceIdentifier& device
        ) {
            ExecuteNotification([&](NotificationContext& notification) {
                notification.WithObservers<IStateSubscriptionRegistryObserver>(
                    [&](IStateSubscriptionRegistryObserver* observer) {
                        observer->OnStateUnsubscribed(typeId, scope, device);
                    }
                );
            });
        }

        void CapacityExhausted(
            StateTypeId typeId,
            StateSubscriptionScope scope,
            const DeviceIdentifier& device
        ) {
            ExecuteNotification([&](NotificationContext& notification) {
                notification.WithObservers<IStateSubscriptionRegistryObserver>(
                    [&](IStateSubscriptionRegistryObserver* observer) {
                        observer->OnStateSubscriptionCapacityExhausted(
                            typeId,
                            scope,
                            device
                        );
                    }
                );
            });
        }
    };

    struct Record {
        bool Used = false;
        StateTypeId TypeId = 0;
        StateSubscriptionScope Scope = StateSubscriptionScope::AnyDevice;
        DeviceIdentifier Device{};
    };

    std::array<Record, TCapacity> _records{};
    std::shared_ptr<RegistryObservable> _observable =
        std::make_shared<RegistryObservable>();

public:
    static constexpr std::size_t Capacity = TCapacity;

    Observable::ObserverHandlePtr RegisterObserver(
        Observable::IObserver* observer
    ) {
        return _observable->RegisterObserver(observer);
    }

    void UnregisterObserver(Observable::IObserver* observer) {
        _observable->UnregisterObserver(observer);
    }

    template<typename TDefinition>
    bool Subscribe(
        const StateSubscription<TDefinition>& subscription =
            StateSubscription<TDefinition>::Any()
    ) {
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
                _observable->Subscribed(
                    record.TypeId,
                    record.Scope,
                    record.Device
                );
                return true;
            }
        }

        _observable->CapacityExhausted(
            StateTypeIdOf<TDefinition>,
            subscription.Scope,
            subscription.Device
        );
        return false;
    }

    template<typename TDefinition>
    bool Unsubscribe(
        const StateSubscription<TDefinition>& subscription =
            StateSubscription<TDefinition>::Any()
    ) {
        for (auto& record : _records) {
            if (
                record.Used &&
                record.TypeId == StateTypeIdOf<TDefinition> &&
                record.Scope == subscription.Scope &&
                record.Device == subscription.Device
            ) {
                const StateTypeId typeId = record.TypeId;
                const StateSubscriptionScope scope = record.Scope;
                const DeviceIdentifier device = record.Device;
                record = Record{};
                _observable->Unsubscribed(typeId, scope, device);
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
                (record.Scope == StateSubscriptionScope::AnyDevice ||
                 record.Device == device)
            ) {
                return true;
            }
        }
        return false;
    }

    std::size_t Count() const {
        std::size_t count = 0;
        for (const auto& record : _records) {
            if (record.Used) ++count;
        }
        return count;
    }
};

}
}
