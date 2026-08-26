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

struct FrontGyroscope { using Value = GyroscopeData; static constexpr StateTypeId Id = 0x1001; };
struct RearGyroscope { using Value = GyroscopeData; static constexpr StateTypeId Id = 0x1002; };
struct LedState { using Value = bool; static constexpr StateTypeId Id = 0x1003; };
using Contract = StateContract<FrontGyroscope, RearGyroscope, LedState>;

class StateObserver final :
    public IRemoteStateManagerObserver,
    public IStateSubscriptionRegistryObserver,
    public IStateSubscriberRegistryObserver,
    public IStatePublisherObserver,
    public IStatePublishedObserver<FrontGyroscope>,
    public IStatePublishedObserver<RearGyroscope>,
    public IStatePublicationObserver {
public:
    int Devices = 0, Accepted = 0, Changed = 0, Rejected = 0, Availability = 0;
    int Subscribed = 0, Unsubscribed = 0;
    int RemoteSubscriberAdded = 0, RemoteSubscriberRemoved = 0, RemoteSubscriberDeviceRemoved = 0;
    int Sources = 0, Published = 0;
    int FrontPublished = 0, RearPublished = 0;
    int Pending = 0, Superseded = 0, Acknowledged = 0, StaleAcknowledgements = 0;

    void OnRemoteStateDeviceRegistered(const DeviceIdentifier&) override { ++Devices; }
    void OnRemoteStateAccepted(const DeviceIdentifier&, StateTypeId, StateEpoch, StateRevision, bool changed) override { ++Accepted; if (changed) ++Changed; }
    void OnRemoteStateRejected(const DeviceIdentifier&, StateTypeId, StateEpoch, StateRevision) override { ++Rejected; }
    void OnRemoteStateAvailabilityChanged(const DeviceIdentifier&, RemoteDeviceAvailability, RemoteDeviceAvailability) override { ++Availability; }
    void OnStateSubscribed(StateTypeId, StateSubscriptionScope, const DeviceIdentifier&) override { ++Subscribed; }
    void OnStateUnsubscribed(StateTypeId, StateSubscriptionScope, const DeviceIdentifier&) override { ++Unsubscribed; }
    void OnRemoteStateSubscriberAdded(const DeviceIdentifier&, StateTypeId) override { ++RemoteSubscriberAdded; }
    void OnRemoteStateSubscriberRemoved(const DeviceIdentifier&, StateTypeId) override { ++RemoteSubscriberRemoved; }
    void OnRemoteStateSubscriberDeviceRemoved(const DeviceIdentifier&) override { ++RemoteSubscriberDeviceRemoved; }
    void OnStateSourceRegistered(StateTypeId) override { ++Sources; }
    void OnStatePublished(StateTypeId, StateEpoch, StateRevision) override { ++Published; }
    void OnStatePublished(StateTag<FrontGyroscope>, const StateUpdate<GyroscopeData>&) override { ++FrontPublished; }
    void OnStatePublished(StateTag<RearGyroscope>, const StateUpdate<GyroscopeData>&) override { ++RearPublished; }
    void OnStatePublicationPending(const DeviceIdentifier&, StateTypeId, StateEpoch, StateRevision) override { ++Pending; }
    void OnStatePublicationSuperseded(const DeviceIdentifier&, StateTypeId, StateEpoch, StateRevision, StateEpoch, StateRevision) override { ++Superseded; }
    void OnStatePublicationAcknowledged(const DeviceIdentifier&, StateTypeId, StateEpoch, StateRevision) override { ++Acknowledged; }
    void OnStatePublicationStaleAcknowledgement(const DeviceIdentifier&, StateTypeId, StateEpoch, StateRevision) override { ++StaleAcknowledgements; }
};

int main() {
    static_assert(Contract::StateCount == 3);
    static_assert(Contract::IndexOf<FrontGyroscope>() == 0);
    static_assert(Contract::IndexOf<RearGyroscope>() == 1);
    static_assert(StateTag<FrontGyroscope>::Id != StateTag<RearGyroscope>::Id);

    const uint8_t mac[6] = {0x64,0xB7,0x08,0x85,0x63,0x3D};
    const uint8_t otherMac[6] = {0xD4,0xD4,0xDA,0x96,0x77,0x81};
    const auto device = DeviceIdentifier::FromMacAddress(mac);
    const auto otherDevice = DeviceIdentifier::FromMacAddress(otherMac);
    uint8_t recoveredMac[6] = {};
    assert(device.TryGetMacAddress(recoveredMac));
    for (std::size_t i = 0; i < 6; ++i) assert(recoveredMac[i] == mac[i]);

    StateObserver observer;
    RemoteStateManager<Contract, 2> manager;
    auto managerHandle = manager.RegisterObserver(static_cast<IRemoteStateManagerObserver*>(&observer));
    assert(manager.Apply<FrontGyroscope>(device, 1, 1, {1,2,3}));
    assert(observer.Devices == 1 && observer.Accepted == 1 && observer.Changed == 1);

    RemoteStateSnapshot<GyroscopeData> front;
    assert(manager.Read<FrontGyroscope>(device, front));
    assert((front.Value == GyroscopeData{1,2,3}));
    assert(manager.Apply<RearGyroscope>(device, 1, 1, {4,5,6}));
    assert(manager.Apply<FrontGyroscope>(device, 1, 2, {1,2,3}));
    assert(!manager.Apply<FrontGyroscope>(device, 1, 2, {9,9,9}));
    assert(manager.Apply<FrontGyroscope>(device, 1, 3, {7,8,9}));
    assert(manager.Apply<FrontGyroscope>(device, 2, 1, {10,11,12}));
    assert(manager.SetAvailability(device, RemoteDeviceAvailability::Connected));
    assert(observer.Rejected == 1 && observer.Availability == 1);

    StateSubscriptionRegistry<3> subscriptions;
    auto subscriptionHandle = subscriptions.RegisterObserver(static_cast<IStateSubscriptionRegistryObserver*>(&observer));
    assert(subscriptions.Subscribe<FrontGyroscope>());
    assert(subscriptions.Subscribe<RearGyroscope>(StateSubscription<RearGyroscope>::From(device)));
    assert(subscriptions.IsSubscribed<FrontGyroscope>(otherDevice));
    assert(subscriptions.IsSubscribed(otherDevice, StateTypeIdOf<FrontGyroscope>));
    assert(subscriptions.IsSubscribed(device, StateTypeIdOf<RearGyroscope>));
    assert(!subscriptions.IsSubscribed<RearGyroscope>(otherDevice));
    assert(!subscriptions.IsSubscribed(otherDevice, StateTypeIdOf<RearGyroscope>));
    assert(subscriptions.Unsubscribe<FrontGyroscope>());
    assert(observer.Subscribed == 2 && observer.Unsubscribed == 1);

    StateSubscriberRegistry<Contract, 2> remoteSubscribers;
    auto remoteSubscriberHandle = remoteSubscribers.RegisterObserver(static_cast<IStateSubscriberRegistryObserver*>(&observer));
    assert(!remoteSubscribers.HasSubscribers<FrontGyroscope>());
    assert(!remoteSubscribers.HasSubscribers(StateTypeIdOf<FrontGyroscope>));
    assert(remoteSubscribers.Subscribe(otherDevice, StateTypeIdOf<FrontGyroscope>));
    assert(remoteSubscribers.IsSubscribed<FrontGyroscope>(otherDevice));
    assert(remoteSubscribers.HasSubscribers<FrontGyroscope>());
    assert(remoteSubscribers.HasSubscribers(StateTypeIdOf<FrontGyroscope>));
    assert(!remoteSubscribers.HasSubscribers<RearGyroscope>());
    assert(observer.RemoteSubscriberAdded == 1);
    assert(remoteSubscribers.Unsubscribe(otherDevice, StateTypeIdOf<FrontGyroscope>));
    assert(!remoteSubscribers.HasSubscribers<FrontGyroscope>());
    assert(observer.RemoteSubscriberRemoved == 1);
    assert(remoteSubscribers.Subscribe(otherDevice, StateTypeIdOf<RearGyroscope>));
    assert(remoteSubscribers.HasSubscribers<RearGyroscope>());
    assert(remoteSubscribers.Remove(otherDevice));
    assert(!remoteSubscribers.HasSubscribers<RearGyroscope>());
    assert(observer.RemoteSubscriberDeviceRemoved == 1);

    GyroscopeData authoritative{20,21,22};
    StatePublisher<Contract> publisher(device, 7);
    auto publisherHandle = publisher.RegisterObserver(
        static_cast<IStatePublisherObserver*>(&observer)
    );
    auto frontPublisherHandle = publisher.RegisterPublishedObserver<FrontGyroscope>(
        static_cast<IStatePublishedObserver<FrontGyroscope>*>(&observer)
    );
    auto rearPublisherHandle = publisher.RegisterPublishedObserver<RearGyroscope>(
        static_cast<IStatePublishedObserver<RearGyroscope>*>(&observer)
    );
    assert(publisher.RegisterSource<FrontGyroscope>([&] { return authoritative; }));
    assert(publisher.RegisterSource<RearGyroscope>([&] { return GyroscopeData{30,31,32}; }));
    assert(observer.Sources == 2);
    assert(publisher.Publish<FrontGyroscope>());
    assert(publisher.Publish<RearGyroscope>());
    assert(observer.Published == 2);
    assert(observer.FrontPublished == 1);
    assert(observer.RearPublished == 1);

    StateUpdate<GyroscopeData> snapshot;
    assert(publisher.Snapshot<FrontGyroscope>(snapshot));
    assert(snapshot.Header.Origin == device);
    assert(snapshot.Header.Epoch == 7);
    assert(snapshot.Header.Revision == 1);
    assert(snapshot.Value == authoritative);

    std::array<uint8_t, 128> wire{};
    std::size_t wireSize = 0;
    assert(StateProtocol::EncodeUpdate<FrontGyroscope>(snapshot, wire.data(), wire.size(), wireSize));
    StateProtocol::ParsedUpdate parsed;
    assert(StateProtocol::DecodeUpdate(wire.data(), wireSize, parsed));
    GyroscopeData decoded;
    assert(StateProtocol::DecodeValue<FrontGyroscope>(parsed, decoded));
    assert(decoded == authoritative);

    StatePublicationTracker tracker;
    auto publicationHandle = tracker.RegisterObserver(static_cast<IStatePublicationObserver*>(&observer));
    assert(tracker.MarkPending(otherDevice, snapshot.Header));
    assert(observer.Pending == 1);
    StateHeader newer = snapshot.Header;
    newer.Revision = 2;
    assert(tracker.MarkPending(otherDevice, newer));
    assert(observer.Superseded == 1 && observer.Pending == 2);
    assert(tracker.Acknowledge(otherDevice, StateTypeIdOf<FrontGyroscope>, 7, 1) == StateAcknowledgementResult::Stale);
    assert(observer.StaleAcknowledgements == 1);
    assert(tracker.Acknowledge(otherDevice, StateTypeIdOf<FrontGyroscope>, 7, 2) == StateAcknowledgementResult::Acknowledged);
    assert(observer.Acknowledged == 1);
    assert(tracker.PendingCount() == 0);

    return 0;
}
