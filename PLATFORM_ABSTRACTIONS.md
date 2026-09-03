# Platform Abstractions Audit Trail

This file records the current State platform-abstraction boundary after the Mesh structural realignment.

## Current audit result

- Core State types, contracts, local binding, remote replicas, protocol and diagnostics contain no required ESP32/Arduino/ESP-IDF/FreeRTOS runtime dependency.
- Canonical `DeviceIdentifier` is owned by `ESPressio-System`; State imports that platform-neutral identity and contains no MAC/radio derivation helper.
- State synchronization and memory behavior use ESPressio-System abstractions rather than platform APIs.
- `RemoteStateObserverThread` delegates execution and wake-up behavior to ESPressio Threads `PrecisionThread`; State does not own a native task/runtime primitive.
- Optional introspection/diagnostic serialization remains platform-neutral and does not introduce a concrete transport dependency.
- Concrete transport/Mesh integration remains outside State core.

## Package metadata

`library.json` advertises the State core as framework- and platform-neutral. Its mandatory dependencies are ESPressio System and ESPressio Observable. Optional deferred execution is selected only by including the Threads-backed observer header.

## Boundary rule

State owns State definition, application-owned local binding, publication lifecycle, observation, remote replication, availability and subscription semantics. Hardware/runtime execution, physical transport, Mesh routing/membership and authenticated transport provenance remain delegated to their owning ESPressio layers.
