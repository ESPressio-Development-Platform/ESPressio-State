# Observation

State uses ESPressio Observable for lightweight synchronous lifecycle notification.

Observer contracts cover meaningful transitions including remote registration, accepted/rejected revisions, availability changes, subscriptions, source registration, publication and pending/acknowledgement lifecycle.

Typed callbacks carry `StateTag<TDefinition>` so definitions sharing one payload type remain distinct.

## Synchronous observers

Core observers may execute in infrastructure or transport-related contexts. Keep them lightweight and avoid arbitrary application work that could block State ingestion.

## Coalesced application observers

When ESPressio Threads is selected, `RemoteStateObserverThread` provides an optional application execution layer.

```cpp
#include <ESPressio_RemoteStateObserverThread.hpp>

RemoteStateObserverThread<DeviceStateContract, 8> observerThread(remoteState);
```

It records dirty `(device, StateType)` identities and later reads the newest repository snapshot in its own execution context.

```text
application last received v9
v10 accepted
v11 accepted
v12 accepted
      |
      v
one dirty identity
      |
observer thread
      |
      v
previous=v9, latest=v12
```

This deliberately does not create a queue of obsolete State values.

## Optional dependency

Threads is not mandatory for State core. It is required only by code selecting the coalesced observer header.

Use Event instead if the application must preserve every intermediate transition.