# ESPressio State 1.0.0

ESPressio State communicates **what is true now** across components and devices.

It is intentionally distinct from Event history and Command execution:

```text
Command  "Please do this."
Event    "This happened."
State    "This is true now."
```

When revisions advance rapidly, obsolete intermediate State may be discarded/coalesced; the newest authoritative value is what matters.

## Core model

- compile-time `StateContract` definitions and typed State identity;
- transport-neutral 128-bit `DeviceIdentifier`;
- fixed-capacity typed `RemoteStateManager` repositories;
- source-backed `StatePublisher` without a duplicate local repository;
- local subscriptions and remote-subscriber registries;
- epoch/revision ordering;
- latest-only pending delivery and acknowledgements;
- availability and resynchronisation;
- synchronous Observable lifecycle notifications;
- optional coalesced application observers;
- optional introspection/diagnostic serialization.

## Start here

- [Getting Started](Getting-Started)
- [State Contracts and Identity](State-Contracts-and-Identity)
- [Publishing State](Publishing-State)
- [Remote State](Remote-State)
- [Subscriptions and Reliability](Subscriptions-and-Reliability)
- [Availability and Resynchronisation](Availability-and-Resynchronisation)
- [Observation](Observation)
- [Transport Protocol](Transport-Protocol)
- [Introspection and Diagnostics](Introspection-and-Diagnostics)
- [Extending State](Extending-State)
- [API Map](API-Map)

## Version baseline

This Wiki documents the intended ESPressio **1.0.0** baseline from the active State foundation architecture rather than its historical development version labels.