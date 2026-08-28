# Subscriptions and Reliability

State communicates only the facts consumers have declared interest in.

## Local subscriptions

`StateSubscriptionRegistry<TCapacity>` records State this device wants to consume.

```cpp
StateSubscriptionRegistry<8> subscriptions;

subscriptions.Subscribe<FrontGyroscope>();
subscriptions.Subscribe<RearGyroscope>(
    StateSubscription<RearGyroscope>::From(specificDevice)
);
```

Subscriptions can target any origin for one State definition or one specific origin/State pair.

Incoming State should be rejected unless `(origin, StateTypeId)` matches a local subscription. A syntactically valid wire message alone is not permission to mutate the remote repository.

## Remote subscribers

`StateSubscriberRegistry<TContract, TMaximumSubscribers>` tracks remote devices that requested State owned by this device. Publishers/transports can therefore avoid sending State for which nobody has declared interest.

## Acknowledgements

An ACK means the receiving remote repository accepted that revision, or already contains that exact revision.

It does **not** mean arbitrary application observers finished reacting.

## Superseding pending revisions

Pending delivery is keyed by destination and State identity. Newer revisions replace older pending revisions.

An ACK for revision 42 cannot clear a newer pending revision 43.

Reliability therefore preserves the newest truth without turning State into Event history.