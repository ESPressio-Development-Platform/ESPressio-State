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
    int Devices = 0, Accepted = 0, Changed = 0, Rejected = 0, Availability = 0;
    int Subscribed = 0, Unsubscribed = 0, RemoteSubscriberAdded = 0, RemoteSubscriberRemoved = 0, RemoteSubscriberDeviceRemoved = 0;
    int SourcesBound = 0, SourcesUnbound = 0, LocalAvailability = 0, Published = 0, FrontPublished = 0, RearPublished = 0;
    void OnRemoteStateDeviceRegistered(const DeviceIdentifier&) override { ++Devices; }
    void OnRemoteStateAccepted(const DeviceIdentifier&, StateTypeId, StateEpoch, StateRevision, bool changed) override { ++Accepted; if (changed) ++Changed; }
    void OnRemoteStateRejected(const DeviceIdentifier&, StateTypeId, StateEpoch, StateRevision) override { ++Rejected; }
    void OnRemoteStateAvailabilityChanged(const StateAddress&, StateAvailabilityStatus, StateAvailabilityStatus) override { ++Availability; }
    void OnStateSubscribed(StateTypeId, StateSubscriptionScope, const DeviceIdentifier&) override { ++Subscribed; }
    void OnStateUnsubscribed(StateTypeId, StateSubscriptionScope, const DeviceIdentifier&) override { ++Unsubscribed; }
    void OnRemoteStateSubscriberAdded(const DeviceIdentifier&, StateTypeId) override { ++RemoteSubscriberAdded; }
    void OnRemoteStateSubscriberRemoved(const DeviceIdentifier&, StateTypeId) override { ++RemoteSubscriberRemoved; }
    void OnRemoteStateSubscriberDeviceRemoved(const DeviceIdentifier&) override { ++RemoteSubscriberDeviceRemoved; }
    void OnStateSourceBound(const StateAddress&, StateEpoch, StateRevision) override { ++SourcesBound; }
    void OnStateSourceUnbound(const StateAddress&, StateUnbindMode, StateEpoch, StateRevision) override { ++SourcesUnbound; }
    void OnStateAvailabilityChanged(const StateAddress&, StateAvailabilityStatus, StateAvailabilityStatus) override { ++LocalAvailability; }
    void OnStatePublished(const StateAddress&, StateEpoch, StateRevision) override { ++Published; }
    void OnStatePublished(StateTag<FrontGyroscope>, const StateUpdate<GyroscopeData>&) override { ++FrontPublished; }
    void OnStatePublished(StateTag<RearGyroscope>, const StateUpdate<GyroscopeData>&) override { ++RearPublished; }
};

class ReentrantSubscriptionObserver final : public IStateSubscriptionRegistryObserver {
public:
    StateSubscriptionRegistry<3>* Registry = nullptr;
    DeviceIdentifier ProbeDevice{};
    bool SawSubscribedState = false;
    bool SawUnsubscribedState = false;
    void OnStateSubscribed(StateTypeId typeId, StateSubscriptionScope, const DeviceIdentifier&) override {
        SawSubscribedState = Registry->IsSubscribed(ProbeDevice, typeId);
        assert(Registry->Count() > 0);
    }
    void OnStateUnsubscribed(StateTypeId typeId, StateSubscriptionScope, const DeviceIdentifier&) override {
        SawUnsubscribedState = !Registry->IsSubscribed(ProbeDevice, typeId);
    }
};

class EmptyRemoteObserver final : public IRemoteStateManagerObserver {};
class EmptySubscriptionObserver final : public IStateSubscriptionRegistryObserver {};
class EmptySubscriberObserver final : public IStateSubscriberRegistryObserver {};

static DeviceIdentifier MakeDevice(uint8_t discriminator) {
    DeviceIdentifier::Storage bytes{};
    bytes[15] = discriminator;
    return DeviceIdentifier(bytes);
}

int main() {
    static_assert(Contract::StateCount == 3);
    static_assert(Contract::IndexOf<FrontGyroscope>() == 0);
    const auto device = MakeDevice(1);
    const auto otherDevice = MakeDevice(2);
    assert(device && otherDevice && device != otherDevice);

    // Observer capacities are independent of data capacities and are released
    // deterministically when the corresponding RAII handle is destroyed.
    EmptyRemoteObserver remoteObserverA, remoteObserverB;
    RemoteStateManager<Contract, 1, 1> boundedManager;
    auto boundedManagerHandle = boundedManager.RegisterObserver(&remoteObserverA);
    assert(boundedManagerHandle);
    assert(!boundedManager.RegisterObserver(&remoteObserverB));
    boundedManagerHandle.reset();
    assert(boundedManager.RegisterObserver(&remoteObserverB));

    EmptySubscriptionObserver subscriptionObserverA, subscriptionObserverB;
    StateSubscriptionRegistry<1, 1> boundedSubscriptions;
    auto boundedSubscriptionHandle = boundedSubscriptions.RegisterObserver(&subscriptionObserverA);
    assert(boundedSubscriptionHandle);
    assert(!boundedSubscriptions.RegisterObserver(&subscriptionObserverB));
    boundedSubscriptionHandle.reset();
    assert(boundedSubscriptions.RegisterObserver(&subscriptionObserverB));

    EmptySubscriberObserver subscriberObserverA, subscriberObserverB;
    StateSubscriberRegistry<Contract, 1, 1> boundedSubscribers;
    auto boundedSubscriberHandle = boundedSubscribers.RegisterObserver(&subscriberObserverA);
    assert(boundedSubscriberHandle);
    assert(!boundedSubscribers.RegisterObserver(&subscriberObserverB));
    boundedSubscriberHandle.reset();
    assert(boundedSubscribers.RegisterObserver(&subscriberObserverB));

    StateObserver observer;
    RemoteStateManager<Contract, 2> manager;
    auto managerHandle = manager.RegisterObserver(static_cast<IRemoteStateManagerObserver*>(&observer));
    assert(manager.Apply<FrontGyroscope>(device, 1, 1, {1,2,3}));
    RemoteStateSnapshot<GyroscopeData> front;
    assert(manager.Read<FrontGyroscope>(device, front));
    assert(manager.Apply<RearGyroscope>(device, 1, 1, {4,5,6}));
    assert(manager.Apply<FrontGyroscope>(device, 1, 2, {1,2,3}));
    assert(!manager.Apply<FrontGyroscope>(device, 1, 2, {9,9,9}));
    assert(manager.SetReachability(device, StateSourceReachability::Reachable));

    StateSubscriptionRegistry<3> subscriptions;
    auto subscriptionHandle = subscriptions.RegisterObserver(static_cast<IStateSubscriptionRegistryObserver*>(&observer));
    ReentrantSubscriptionObserver reentrantObserver;
    reentrantObserver.Registry = &subscriptions;
    reentrantObserver.ProbeDevice = otherDevice;
    auto reentrantHandle = subscriptions.RegisterObserver(&reentrantObserver);
    assert(subscriptions.Subscribe<FrontGyroscope>());
    assert(subscriptions.Subscribe<RearGyroscope>(StateSubscription<RearGyroscope>::From(device)));
    assert(subscriptions.IsSubscribed<FrontGyroscope>(otherDevice));
    assert(!subscriptions.IsSubscribed<RearGyroscope>(otherDevice));
    assert(subscriptions.Unsubscribe<FrontGyroscope>());

    StateSubscriberRegistry<Contract, 2> remoteSubscribers;
    auto remoteSubscriberHandle = remoteSubscribers.RegisterObserver(static_cast<IStateSubscriberRegistryObserver*>(&observer));
    assert(remoteSubscribers.Subscribe(otherDevice, StateTypeIdOf<FrontGyroscope>));
    assert(remoteSubscribers.Unsubscribe(otherDevice, StateTypeIdOf<FrontGyroscope>));
    assert(remoteSubscribers.Subscribe(otherDevice, StateTypeIdOf<RearGyroscope>));
    assert(remoteSubscribers.Remove(otherDevice));

    GyroscopeData authoritative{20,21,22};
    StatePublisher<Contract, 2> publisher(device);
    auto publisherHandle = publisher.RegisterObserver(static_cast<IStatePublisherObserver*>(&observer));
    auto frontPublisherHandle = publisher.RegisterPublishedObserver<FrontGyroscope>(static_cast<IStatePublishedObserver<FrontGyroscope>*>(&observer));
    assert(publisherHandle && frontPublisherHandle);
    assert(!publisher.RegisterPublishedObserver<RearGyroscope>(static_cast<IStatePublishedObserver<RearGyroscope>*>(&observer)));
    frontPublisherHandle.reset();
    auto rearPublisherHandle = publisher.RegisterPublishedObserver<RearGyroscope>(static_cast<IStatePublishedObserver<RearGyroscope>*>(&observer));
    assert(rearPublisherHandle);

    assert(publisher.Bind<FrontGyroscope>(authoritative));
    auto localRegistration = publisher.Registration<FrontGyroscope>();
    assert(localRegistration.Bound && localRegistration.Epoch == 1 && localRegistration.Revision == 1);

    StateUpdate<GyroscopeData> snapshot;
    assert(publisher.Snapshot<FrontGyroscope>(snapshot));
    assert(snapshot.Header.Origin == device && snapshot.Header.Epoch == 1 && snapshot.Header.Revision == 1);
    assert(snapshot.Value == authoritative);

    authoritative = {30,31,32};
    StateRevision committedRevision = 0;
    assert(publisher.NotifyChanged<FrontGyroscope>(&committedRevision));
    assert(committedRevision == 2);
    assert(observer.Published == 1);
    assert(observer.FrontPublished == 0); // typed Front observer was deliberately released above.

    assert(publisher.Unbind<FrontGyroscope>(StateUnbindMode::Retain));
    assert(observer.SourcesBound == 1 && observer.SourcesUnbound == 1);
    assert(observer.LocalAvailability == 2);
    authoritative = {40,41,42};
    assert(publisher.Bind<FrontGyroscope>(authoritative));
    assert(publisher.Registration<FrontGyroscope>().Revision == 3);
    assert(publisher.Snapshot<FrontGyroscope>(snapshot));
    assert(snapshot.Header.Revision == 3 && snapshot.Value == authoritative);

    StatePublicationTracker<FrontGyroscope> tracker(otherDevice);
    assert(tracker.Replace(1, 1, {1,1,1}));
    assert(tracker.Replace(1, 2, {2,2,2}));
    assert(tracker.PendingUpdate().Pending && tracker.PendingUpdate().Revision == 2);
    tracker.Clear();
    assert(!tracker.PendingUpdate().Pending);
    return 0;
}
