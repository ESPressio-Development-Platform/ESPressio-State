#include <cassert>
#include <cstdint>

#include <ESPressio_State.hpp>

using namespace ESPressio::State;

struct GyroscopeData {
    int16_t X = 0; int16_t Y = 0; int16_t Z = 0;
    bool operator==(const GyroscopeData& other) const { return X == other.X && Y == other.Y && Z == other.Z; }
};
struct FrontGyroscope { using Value = GyroscopeData; static constexpr StateTypeId Id = 0x1001; };
struct RearGyroscope { using Value = GyroscopeData; static constexpr StateTypeId Id = 0x1002; };
struct LedState { using Value = bool; static constexpr StateTypeId Id = 0x1003; };
using Contract = StateContract<FrontGyroscope, RearGyroscope, LedState>;

class StateObserver final : public IRemoteStateManagerObserver, public IStateSubscriptionRegistryObserver,
    public IStateSubscriberRegistryObserver, public IStatePublisherObserver,
    public IStatePublishedObserver<FrontGyroscope>, public IStatePublishedObserver<RearGyroscope> {
public:
    int Devices = 0, Accepted = 0, Changed = 0, Rejected = 0, Availability = 0, Reachability = 0;
    int Subscribed = 0, Unsubscribed = 0, RemoteSubscriberAdded = 0, RemoteSubscriberRemoved = 0, RemoteSubscriberDeviceRemoved = 0;
    int Sources = 0, Published = 0, FrontPublished = 0, RearPublished = 0;
    void OnRemoteStateDeviceRegistered(const DeviceIdentifier&) override { ++Devices; }
    void OnRemoteStateAccepted(const DeviceIdentifier&, StateTypeId, StateEpoch, StateRevision, bool changed) override { ++Accepted; if (changed) ++Changed; }
    void OnRemoteStateRejected(const DeviceIdentifier&, StateTypeId, StateEpoch, StateRevision) override { ++Rejected; }
    void OnRemoteStateAvailabilityChanged(const StateAddress&, StateAvailabilityStatus, StateAvailabilityStatus) override { ++Availability; }
    void OnRemoteStateReachabilityChanged(const DeviceIdentifier&, StateSourceReachability, StateSourceReachability) override { ++Reachability; }
    void OnStateSubscribed(StateTypeId, StateSubscriptionScope, const DeviceIdentifier&) override { ++Subscribed; }
    void OnStateUnsubscribed(StateTypeId, StateSubscriptionScope, const DeviceIdentifier&) override { ++Unsubscribed; }
    void OnRemoteStateSubscriberAdded(const DeviceIdentifier&, StateTypeId) override { ++RemoteSubscriberAdded; }
    void OnRemoteStateSubscriberRemoved(const DeviceIdentifier&, StateTypeId) override { ++RemoteSubscriberRemoved; }
    void OnRemoteStateSubscriberDeviceRemoved(const DeviceIdentifier&) override { ++RemoteSubscriberDeviceRemoved; }
    void OnStateSourceRegistered(StateTypeId) override { ++Sources; }
    void OnStatePublished(StateTypeId, StateEpoch, StateRevision) override { ++Published; }
    void OnStatePublished(StateTag<FrontGyroscope>, const StateUpdate<GyroscopeData>&) override { ++FrontPublished; }
    void OnStatePublished(StateTag<RearGyroscope>, const StateUpdate<GyroscopeData>&) override { ++RearPublished; }
};

class ReentrantSubscriptionObserver final : public IStateSubscriptionRegistryObserver {
public:
    StateSubscriptionRegistry<3>* Registry = nullptr; DeviceIdentifier ProbeDevice{}; bool SawSubscribedState = false; bool SawUnsubscribedState = false;
    void OnStateSubscribed(StateTypeId typeId, StateSubscriptionScope, const DeviceIdentifier&) override { SawSubscribedState = Registry->IsSubscribed(ProbeDevice, typeId); assert(Registry->Count() > 0); }
    void OnStateUnsubscribed(StateTypeId typeId, StateSubscriptionScope, const DeviceIdentifier&) override { SawUnsubscribedState = !Registry->IsSubscribed(ProbeDevice, typeId); }
};

static DeviceIdentifier MakeDevice(uint8_t discriminator) {
    DeviceIdentifier::Storage bytes{}; bytes[15] = discriminator; return DeviceIdentifier(bytes);
}

int main() {
    static_assert(Contract::StateCount == 3);
    static_assert(Contract::IndexOf<FrontGyroscope>() == 0);
    const auto device = MakeDevice(1); const auto otherDevice = MakeDevice(2);
    assert(device && otherDevice && device != otherDevice);
    const StateAddress frontAddress = MakeStateAddress<FrontGyroscope>(device);
    assert(frontAddress && frontAddress.Device == device && frontAddress.TypeId == FrontGyroscope::Id);

    StateObserver observer;
    RemoteStateManager<Contract, 2> manager;
    auto managerHandle = manager.RegisterObserver(static_cast<IRemoteStateManagerObserver*>(&observer));
    assert(manager.SetReachability(device, StateSourceReachability::Reachable));
    assert(manager.Apply<FrontGyroscope>(device, 1, 1, {1,2,3}));
    RemoteStateSnapshot<GyroscopeData> front; assert(manager.Read<FrontGyroscope>(device, front));
    assert(front.Availability.Availability == StateAvailability::Available);
    assert(front.Availability.Reason == StateAvailabilityReason::None);
    assert(manager.Apply<RearGyroscope>(device, 1, 1, {4,5,6}));
    assert(manager.Apply<FrontGyroscope>(device, 1, 2, {1,2,3}));
    assert(!manager.Apply<FrontGyroscope>(device, 1, 2, {9,9,9}));
    assert(manager.ApplyAvailability<FrontGyroscope>(device, StateAvailability::Unavailable, StateAvailabilityReason::SourceUnbound));
    assert(manager.Read<FrontGyroscope>(device, front));
    assert(front.Availability.Availability == StateAvailability::Unavailable);
    assert(front.Availability.Reason == StateAvailabilityReason::SourceUnbound);
    assert(manager.ApplyAvailability<FrontGyroscope>(device, StateAvailability::Available));
    assert(manager.SetReachability(device, StateSourceReachability::Unreachable));
    assert(manager.Read<FrontGyroscope>(device, front));
    assert(front.Availability.Availability == StateAvailability::Unavailable);
    assert(front.Availability.Reason == StateAvailabilityReason::SourceUnreachable);

    StateSubscriptionRegistry<3> subscriptions;
    auto subscriptionHandle = subscriptions.RegisterObserver(static_cast<IStateSubscriptionRegistryObserver*>(&observer));
    ReentrantSubscriptionObserver reentrantObserver; reentrantObserver.Registry = &subscriptions; reentrantObserver.ProbeDevice = otherDevice;
    auto reentrantHandle = subscriptions.RegisterObserver(&reentrantObserver);
    assert(subscriptions.Subscribe<FrontGyroscope>()); assert(subscriptions.Subscribe<RearGyroscope>(StateSubscription<RearGyroscope>::From(device)));
    assert(subscriptions.IsSubscribed<FrontGyroscope>(otherDevice)); assert(!subscriptions.IsSubscribed<RearGyroscope>(otherDevice));
    assert(subscriptions.Unsubscribe<FrontGyroscope>());

    StateSubscriberRegistry<Contract, 2> remoteSubscribers;
    auto remoteSubscriberHandle = remoteSubscribers.RegisterObserver(static_cast<IStateSubscriberRegistryObserver*>(&observer));
    assert(remoteSubscribers.Subscribe(otherDevice, StateTypeIdOf<FrontGyroscope>));
    assert(remoteSubscribers.Unsubscribe(otherDevice, StateTypeIdOf<FrontGyroscope>));
    assert(remoteSubscribers.Subscribe(otherDevice, StateTypeIdOf<RearGyroscope>)); assert(remoteSubscribers.Remove(otherDevice));

    GyroscopeData authoritative{20,21,22};
    StatePublisher<Contract> publisher(device, 7);
    auto publisherHandle = publisher.RegisterObserver(static_cast<IStatePublisherObserver*>(&observer));
    auto frontPublisherHandle = publisher.RegisterPublishedObserver<FrontGyroscope>(static_cast<IStatePublishedObserver<FrontGyroscope>*>(&observer));
    assert(publisher.RegisterSource<FrontGyroscope>([&] { return authoritative; }));
    assert(publisher.Publish<FrontGyroscope>());
    StateUpdate<GyroscopeData> snapshot; assert(publisher.Snapshot<FrontGyroscope>(snapshot));
    assert(snapshot.Header.Origin == device && snapshot.Header.Epoch == 7 && snapshot.Header.Revision == 1);

    StatePublicationTracker<FrontGyroscope> tracker(otherDevice);
    assert(tracker.Replace(7, 1, {1,1,1}));
    assert(tracker.Replace(7, 2, {2,2,2}));
    assert(tracker.PendingUpdate().Pending && tracker.PendingUpdate().Revision == 2);
    tracker.Clear(); assert(!tracker.PendingUpdate().Pending);
    return 0;
}
