# ESPressio State dependency boundaries

ESPressio State owns authoritative-fact, local binding and remote-replica semantics while remaining independent of concrete transport, Mesh routing and platform APIs.

## Mandatory foundation dependencies

State core depends on:

- `ESPressio-System` for canonical `DeviceIdentifier`, synchronization and memory abstractions;
- `ESPressio-Primitive` for the stable State family identity and common conceptual-message vocabulary;
- `ESPressio-Observable` for bounded multi-observer lifecycle/publication surfaces.

The propagation branch pins System and Primitive to `structural_realignment_propagation_ESPressio-Mesh` and Observable to its current `structural_realignment` Working Branch.

State core owns:

- `StateAddress = DeviceIdentifier + StateTypeId`;
- `StateContract` and typed definition identities;
- non-owning application-owned local State binding and registration epoch/revision lineage;
- bounded remote State replica storage;
- source-authoritative State availability and effective availability resolution;
- bounded subscription and remote-subscriber registries;
- transport-neutral Publication, Availability, Subscribe, Unsubscribe, Resynchronize and reciprocal subscription-result message contracts.

State core does **not** own a second device identity, MAC/radio identity, Mesh membership/routing, baseline replica acknowledgements, generic Disconnect semantics or concrete transport behavior.

## Optional execution / observation

`ESPressio_RemoteStateObserverThread.hpp` optionally depends on ESPressio Threads/Timing/Units to move application observer execution away from a receive/infrastructure context. This dependency is selected only by consumers of that optional header and is not a mandatory State package dependency.

State-owned observer registrations remain explicitly bounded even though their common multi-observer mechanism comes from ESPressio Observable.

## Optional serialization / introspection

Human-readable State names, introspection and diagnostic serialization are opt-in. They do not alter State identity and do not establish a second transport protocol.

## Concrete transport and Mesh integration

Concrete State transport integration does not belong in State core.

- Mesh transport adaptation belongs in `ESPressio-MeshAdapters`, above State and Mesh. It validates `StateAddress.DeviceIdentifier` against the authenticated Mesh source before applying remote State.
- Technology-specific direct integrations, where retained outside Mesh, belong with the concrete transport-owning repository rather than introducing hardware dependencies into State.
- Serial/Web/debug integrations belong in their consuming libraries.

## Primitive boundary

State reports what is true now.
Event reports that something happened.
Command requests that something happen.

State must not absorb Event history or Command execution semantics. The common conceptual-message vocabulary is owned separately by `ESPressio-Primitive`; its integration into State is part of the surrounding Mesh structural-realignment tranche rather than a reason for State core to depend upward on MeshAdapters.
