#include <cassert>
#include <cstdint>

#include <ESPressio_State.hpp>

using namespace ESPressio::State;

struct GyroscopeData {
    int16_t X = 0;
    int16_t Y = 0;
    int16_t Z = 0;

    bool operator==(const GyroscopeData& other) const {
        return X == other.X && Y == other.Y && Z == other.Z;
    }
};

struct FrontGyroscope {
    using Value = GyroscopeData;
    static constexpr StateTypeId Id = 0x1001;
};

struct RearGyroscope {
    using Value = GyroscopeData;
    static constexpr StateTypeId Id = 0x1002;
};

struct LedState {
    using Value = bool;
    static constexpr StateTypeId Id = 0x1003;
};

using Contract = StateContract<FrontGyroscope, RearGyroscope, LedState>;

class StateObserver final :
    public IRemoteStateManagerObserver,
    public IStateSubscriptionRegistryObserver {
public:
    int Devices = 0;
    int Accepted = 0;
    int Changed = 0;
    int Rejected = 0;
    int Availability = 0;
    int Subscribed = 0;
    int Unsubscribed = 0;

    void OnRemoteStateDeviceRegistered(const DeviceIdentifier&) override {
        ++Devices;
    }

    void OnRemoteStateAccepted(
        const DeviceIdentifier&,
        StateTypeId,
        StateEpoch,
        StateRevision,
        bool changed
    ) override {
        ++Accepted;
        if (changed) ++Changed;
    }

    void OnRemoteStateRejected(
        const DeviceIdentifier&,
        StateTypeId,
        StateEpoch,
        StateRevision
    ) override {
        ++Rejected;
    }

    void OnRemoteStateAvailabilityChanged(
        const DeviceIdentifier&,
        RemoteDeviceAvailability,
        RemoteDeviceAvailability
    ) override {
        ++Availability;
    }

    void OnStateSubscribed(
        StateTypeId,
        StateSubscriptionScope,
        const DeviceIdentifier&
    ) override {
        ++Subscribed;
    }

    void OnStateUnsubscribed(
        StateTypeId,
        StateSubscriptionScope,
        const DeviceIdentifier&
    ) override {
        ++Unsubscribed;
    }
};

int main() {
    static_assert(Contract::StateCount == 3);
    static_assert(Contract::Contains<FrontGyroscope>);
    static_assert(Contract::IndexOf<FrontGyroscope>() == 0);
    static_assert(Contract::IndexOf<RearGyroscope>() == 1);
    static_assert(Contract::IndexOf<LedState>() == 2);

    const uint8_t mac[6] = {0x64, 0xB7, 0x08, 0x85, 0x63, 0x3D};
    const auto device = DeviceIdentifier::FromMacAddress(mac);
    assert(!device.IsZero());
    assert(device.Bytes()[10] == mac[0]);
    assert(device.Bytes()[15] == mac[5]);

    const uint8_t otherMac[6] = {0xD4, 0xD4, 0xDA, 0x96, 0x77, 0x81};
    const auto otherDevice = DeviceIdentifier::FromMacAddress(otherMac);

    StateObserver observer;
    RemoteStateManager<Contract, 2> manager;
    auto managerHandle = manager.RegisterObserver(&observer);
    assert(managerHandle);
    assert(manager.GetDeviceCount() == 0);

    assert(manager.Apply<FrontGyroscope>(device, 1, 1, {1, 2, 3}));
    assert(manager.GetDeviceCount() == 1);
    assert(observer.Devices == 1);
    assert(observer.Accepted == 1);
    assert(observer.Changed == 1);

    RemoteStateSnapshot<GyroscopeData> front;
    assert(manager.Read<FrontGyroscope>(device, front));
    assert(front.HasValue);
    assert(front.Value == GyroscopeData{1, 2, 3});

    // Same payload type, different compile-time State identity and storage.
    assert(manager.Apply<RearGyroscope>(device, 1, 1, {4, 5, 6}));
    RemoteStateSnapshot<GyroscopeData> rear;
    assert(manager.Read<RearGyroscope>(device, rear));
    assert(rear.Value == GyroscopeData{4, 5, 6});
    assert(manager.Read<FrontGyroscope>(device, front));
    assert(front.Value == GyroscopeData{1, 2, 3});

    // A newer revision carrying the same value is accepted but is not a value change.
    assert(manager.Apply<FrontGyroscope>(device, 1, 2, {1, 2, 3}));
    assert(observer.Accepted == 3);
    assert(observer.Changed == 2);

    // Stale and duplicate revisions never replace the latest State.
    assert(!manager.Apply<FrontGyroscope>(device, 1, 2, {9, 9, 9}));
    assert(!manager.Apply<FrontGyroscope>(device, 1, 1, {9, 9, 9}));
    assert(observer.Rejected == 2);
    assert(manager.Read<FrontGyroscope>(device, front));
    assert(front.Value == GyroscopeData{1, 2, 3});

    assert(manager.Apply<FrontGyroscope>(device, 1, 3, {7, 8, 9}));
    assert(manager.Read<FrontGyroscope>(device, front));
    assert(front.Revision == 3);
    assert(front.Value == GyroscopeData{7, 8, 9});

    // A newer epoch establishes a new authoritative State lifetime.
    assert(manager.Apply<FrontGyroscope>(device, 2, 1, {10, 11, 12}));
    assert(manager.Read<FrontGyroscope>(device, front));
    assert(front.Epoch == 2);
    assert(front.Revision == 1);

    assert(manager.SetAvailability(device, RemoteDeviceAvailability::Connected));
    assert(manager.GetAvailability(device) == RemoteDeviceAvailability::Connected);
    assert(observer.Availability == 1);

    StateSubscriptionRegistry<3> subscriptions;
    auto subscriptionHandle = subscriptions.RegisterObserver(&observer);
    assert(subscriptionHandle);

    assert(subscriptions.Subscribe<FrontGyroscope>());
    assert(subscriptions.IsSubscribed<FrontGyroscope>(device));
    assert(subscriptions.IsSubscribed<FrontGyroscope>(otherDevice));

    assert(subscriptions.Subscribe<RearGyroscope>(
        StateSubscription<RearGyroscope>::From(device)
    ));
    assert(subscriptions.IsSubscribed<RearGyroscope>(device));
    assert(!subscriptions.IsSubscribed<RearGyroscope>(otherDevice));
    assert(subscriptions.Count() == 2);
    assert(observer.Subscribed == 2);

    assert(subscriptions.Unsubscribe<FrontGyroscope>());
    assert(!subscriptions.IsSubscribed<FrontGyroscope>(device));
    assert(observer.Unsubscribed == 1);

    PendingStateUpdate<GyroscopeData> pending;
    pending.Replace(1, 10, {1, 1, 1});
    assert(pending.Pending);
    assert(pending.Revision == 10);

    // A newer State revision replaces stale pending work rather than queuing it.
    pending.Replace(1, 11, {2, 2, 2});
    assert(pending.Revision == 11);
    assert(pending.Value == GyroscopeData{2, 2, 2});

    // A late ACK for the superseded revision does not clear the latest State.
    assert(pending.Acknowledge(1, 10));
    assert(pending.Pending);
    assert(pending.LastAcknowledgedRevision == 10);

    assert(pending.Acknowledge(1, 11));
    assert(!pending.Pending);
    assert(pending.LastAcknowledgedRevision == 11);

    return 0;
}
