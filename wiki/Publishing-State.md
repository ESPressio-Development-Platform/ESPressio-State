# Publishing State

State does not require domain objects to move their authoritative local data into a second repository.

```text
domain owner
 authoritative data
      |
      | typed snapshot accessor
      v
 StatePublisher
      |
      v
 transport/subscribers
```

## Source-backed publication

Register an accessor:

```cpp
StatePublisher<DeviceStateContract> publisher(localDevice);

publisher.RegisterSource<FrontCamera>([&camera] {
    return camera.GetRemoteStateSnapshot();
});
```

When the authoritative owner changes, publish the newest snapshot:

```cpp
publisher.Publish<FrontCamera>();
```

`Snapshot<TDefinition>()` can obtain a fresh source-backed value for initial synchronization/resynchronization.

## Explicit publication

A caller may publish an explicit typed value when it already owns the correct current snapshot:

```cpp
publisher.Publish<FrontCamera>(currentValue);
```

## Revisions and epochs

Publication advances revision ordering. Epoch identifies an origin lifetime, allowing receivers to distinguish a restarted/new publisher lifetime from an old sequence of revisions.

## Latest-only semantics

If revision 43 supersedes pending revision 42 for the same destination/State identity, pending delivery represents 43 rather than retaining a historical queue of both values.

This is fundamental State semantics, not merely a queue optimization.