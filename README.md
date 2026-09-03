# ESPressio State

Strongly typed authoritative-fact and replication infrastructure for the ESPressio Development Platform.

ESPressio State represents **what is true now**. It is deliberately distinct from Command (asynchronous intent) and Event (occurrence/history). Intermediate State revisions may be coalesced because the latest authoritative fact is the semantic result.

This propagation branch implements the platform structural realignment toward the true 1.0.0 baseline. Package version fields are intentionally not changed by this tranche.

## Identity

State uses the canonical permanent `ESPressio::System::DeviceIdentifier`; it does not own or derive a second device identity and contains no MAC/radio identity semantics.

One State is addressed by:

```cpp
StateAddress {
    DeviceIdentifier Device;
    StateTypeId TypeId;
}
```

There is no separate `SourceIdentifier`. Multiple semantic values on one device use different State definitions / `StateTypeId` values.

## State definitions

```cpp
struct TemperatureState {
    using Value = float;
    static constexpr ESPressio::State::StateTypeId Id = 0x1001;
};

using DeviceStateContract = ESPressio::State::StateContract<TemperatureState>;
```

`StateContract<...>` provides a compile-time closed set for typed publishers, replicas, subscriptions and optional introspection. `StateTag<TDefinition>` preserves semantic identity even where several State definitions share the same C++ value type.

## Authoritative local State

Application/domain objects remain the authority for local State. `StatePublisher` and its `LocalStateRegistry` do not own a second canonical value repository: they retain only typed non-owning references plus registration epoch/revision metadata.

```cpp
float temperature = sensor.Temperature();
StatePublisher<DeviceStateContract> publisher(localDevice);

publisher.Bind<TemperatureState>(temperature);
```

The application mutates its own authoritative member and then explicitly declares the semantic change:

```cpp
temperature = sensor.Temperature();
publisher.NotifyChanged<TemperatureState>();
```

`NotifyChanged` is the authoritative publication boundary. It advances that State registration's revision regardless of equality policy, captures one immutable typed `StateUpdate`, and synchronously notifies transport/application observers. Direct local reads continue to dereference the application-owned source rather than a shadow copy.

Initial binding establishes revision 1. If a binding is removed with `StateUnbindMode::Retain`, its epoch/revision lineage remains inactive and a later rebind advances the existing revision before the replacement source becomes visible. This prevents one epoch/revision pair from ever denoting two different values. `StateUnbindMode::Discard` terminates the lineage so the next bind begins a new epoch at revision 1.

```cpp
publisher.Unbind<TemperatureState>(StateUnbindMode::Retain);
publisher.Bind<TemperatureState>(temperature);   // same epoch, next revision

publisher.Unbind<TemperatureState>(StateUnbindMode::Discard);
publisher.Bind<TemperatureState>(temperature);   // new epoch, revision 1
```

Unbinding also emits authoritative `Unavailable / SourceUnbound`; binding emits authoritative `Available`. Availability lifecycle is separate from value publication.

`Snapshot<TDefinition>()` reads the current bound value and current epoch/revision without advancing either. It is intended for initial subscription establishment and resynchronisation.

`StatePublisher<TContract, TMaximumObservers>` has a finite observer-registration capacity; the default is 8 simultaneous registrations. Capacity is independent of the State contract's number of definitions. A contract observer registered against several typed interfaces still consumes one registration.

Same-State publication notification is non-reentrant. If an observer synchronously mutates the same authoritative State and calls `NotifyChanged` again, the nested call commits its revision but does not recursively enter observers. StatePublisher retains at most one externally-preferred immutable deferred snapshot for that State and dispatches it after the active notification returns. Further changes before deferred dispatch replace that pending snapshot with the newest revision, so obsolete intermediate revisions may be coalesced while emitted revisions remain strictly increasing. This is intentional latest-fact behavior; use Event/stream semantics when every transition must be preserved.

A State-definition-specific `StateComparison<TDefinition>` remains available for application/domain code or higher-level State value wrappers that want semantic equality/deadband behavior before deciding whether to call `NotifyChanged`. Reference-backed State deliberately does not retain a hidden shadow value merely to perform equality suppression.

## Remote replicas

Remote values are State-owned and bounded by the manager capacity:

```cpp
RemoteStateManager<DeviceStateContract, 8> remoteState;
```

A received authoritative publication is applied with its independent State epoch and monotonic revision:

```cpp
remoteState.Apply<TemperatureState>(
    sourceDevice,
    epoch,
    revision,
    temperature
);
```

Older or duplicate revisions cannot overwrite newer retained State. `StateEpoch` defines a State publication lineage and is intentionally independent of any Mesh membership incarnation.

Reads return stable snapshots rather than pointers into mutable replica storage:

```cpp
RemoteStateSnapshot<float> snapshot;
if (remoteState.Read<TemperatureState>(sourceDevice, snapshot) && snapshot.HasValue) {
    // snapshot.Value
    // snapshot.Epoch
    // snapshot.Revision
    // snapshot.Availability
    // snapshot.Reachability
}
```

## Availability and reachability

State availability and source reachability are separate concepts.

Authoritative State availability is one of:

```text
Available
Stale
Unavailable
Expired
```

Unavailable reasons currently include `SourceUnbound` and `SourceUnreachable`. Device reachability is tracked independently as `Unknown`, `Reachable`, `Stale` or `Unreachable`.

`RemoteStateManager` combines the authoritative State status with current reachability to produce the effective `StateAvailabilityStatus` returned to consumers. For example, an otherwise Available State becomes effectively `Unavailable / SourceUnreachable` while its authoritative device is unreachable. Reachability changes also produce per-State effective availability transitions for retained State identities.

Authoritative availability received from the State source is applied independently:

```cpp
remoteState.ApplyAvailability<TemperatureState>(
    sourceDevice,
    StateAvailability::Unavailable,
    StateAvailabilityReason::SourceUnbound
);
```

Transport/Mesh integration reports source reachability separately:

```cpp
remoteState.SetReachability(
    sourceDevice,
    StateSourceReachability::Unreachable
);
```

## Observation

Core State lifecycle notification uses ESPressio Observable. The publisher exposes distinct local observations for source binding/unbinding, authoritative availability changes, generic committed publications, and typed committed `StateUpdate` snapshots.

Observer registration is explicitly bounded rather than relying on the general Observable registry as an implicit memory limit. `StatePublisher`, `RemoteStateManager`, `StateSubscriptionRegistry`, `StateSubscriberRegistry`, and the optional `RemoteStateObserverThread` each accept an independent compile-time maximum observer count, defaulting to 8 while preserving their separate data-capacity dimensions. Registration order remains the deterministic callback order supplied by ESPressio Observable.

The remote manager exposes distinct observations for:

- accepted/rejected remote revisions;
- effective State availability changes keyed by `StateAddress`;
- source-device reachability changes;
- remote-device discovery/registration.

Typed remote value observers receive the current effective `StateAvailabilityStatus` alongside epoch/revision information.

The optional `ESPressio_RemoteStateObserverThread.hpp` layer coalesces dirty identities and moves application observer execution onto an ESPressio Threads execution context. Its bookkeeping remains bounded by the manager/device/contract capacities. State, availability and reachability remain separately observable there as well.

## Subscriptions

`StateSubscriptionRegistry<TCapacity>` records State this device wishes to consume. A subscription can target one State definition from any device or from one specific canonical DeviceIdentifier.

`StateSubscriberRegistry<TContract, TMaximumSubscribers>` records remote devices consuming authoritative State from this device.

Transport adapters are responsible for enforcing subscription/admission policy before mutating a remote replica.

## Transport protocol

State owns a compact transport-independent family protocol containing:

```text
Publication
Availability
Subscribe
Unsubscribe
Resynchronize
SubscribeResult
UnsubscribeResult
```

There is deliberately no baseline replica acknowledgement and no generic disconnect message. State replication is asynchronous latest-authoritative-fact propagation rather than RPC/reliable-history delivery.

`Publication` carries canonical source DeviceIdentifier, StateTypeId, StateEpoch, StateRevision and the encoded typed value. `Availability` separately carries canonical State identity plus authoritative `StateAvailability` and `StateAvailabilityReason`. Reachability is not encoded as authoritative State because it is derived from the active transport/Mesh context.

`Resynchronize` asks for the current authoritative fact; it does not request historical replay.

`StateCodec<TDefinition>` is the typed payload codec boundary. The default implementation supports suitable trivially-copyable values; richer State definitions can specialize the codec.

## Latest-only outbound work

`StatePublicationTracker<TDefinition>` retains at most the newest committed outbound publication for one destination. A newer epoch/revision replaces older pending work. There is no acknowledgement lifecycle in the tracker.

This allows State transport to coalesce obsolete intermediate revisions while preserving the latest authoritative value.

## Optional introspection and diagnostic serialization

Core State identity is numeric and does not require names. Applications that need human-readable diagnostics can opt into:

```cpp
#include <ESPressio_StateIntrospection.hpp>
#include <ESPressio_StateSerialization.hpp>
```

A definition may expose an optional diagnostic `Name`; that name never participates in State identity or wire addressing.

Diagnostic serialized snapshots include canonical DeviceIdentifier, StateTypeId, optional name, epoch/revision, effective State availability, source reachability and a bounded encoded payload.

These optional headers do not establish a second transport protocol.

## Dependencies

Mandatory State dependencies on this propagation branch are:

```text
ESPressio-System     structural_realignment_propagation_ESPressio-Mesh
ESPressio-Observable structural_realignment
```

ESPressio Threads is optional and required only by the optional deferred observer execution layer. Event, Mesh, MeshAdapters, Serial, Web and concrete transports remain above or beside State and are not mandatory State-core dependencies.

This preserves dependency direction: State knows nothing about Mesh transport identities, routing, radios or authenticated membership context.

## Integration boundary

Mesh integration belongs in `ESPressio-MeshAdapters`, not in State core. The State Mesh adapter is responsible for validating that a received State address's `DeviceIdentifier` equals the authenticated Mesh source before the publication/availability is accepted by State.

State therefore remains transport-independent while retaining authoritative source identity and typed replication semantics.
