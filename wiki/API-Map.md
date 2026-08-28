# API Map

## Identity and contracts

- `DeviceIdentifier` — transport-neutral 128-bit device identity.
- `StateTypeId` — stable logical State identity.
- `StateContract<...>` — compile-time collection of State definitions.
- `StateTag<TDefinition>` — typed definition identity in callbacks/APIs.

## Publication

- `StatePublisher<TContract>` — source-backed local State publication.
- source registration/snapshot/publication lifecycle.

## Remote repository

- `RemoteStateManager<TContract, N>` — fixed-capacity typed remote State storage.
- `RemoteStateSnapshot<T>` — stable transactional read result.

## Subscription/reliability

- `StateSubscriptionRegistry<N>` — State this device wants to consume.
- `StateSubscriberRegistry<TContract, N>` — remote consumers of locally owned State.
- update/ACK/subscription/resynchronisation/disconnect protocol messages.

## Observation

- Observable-based component lifecycle observers.
- `RemoteStateObserverThread` — optional Threads-backed coalesced application observation.

## Codec and diagnostics

- `StateCodec<TDefinition>` — authoritative typed payload codec boundary.
- `StateIntrospection<TContract>` — optional symbolic/read-only introspection.
- `StateSerialization<TContract>` — optional bounded diagnostic records.

## Core dependency direction

State core depends on ESPressio Observable. Threads, Serial, ESP-Now and other integrations remain optional/downstream selections rather than mandatory core dependencies.