# ESPressio State dependency boundaries

ESPressio State separates transport-neutral State contracts/storage from execution, observation, diagnostics, and concrete transports.

## Core foundation

The following concepts remain dependency-free beyond C++17:

- `DeviceIdentifier`
- `StateContract`
- `RemoteStateManager`
- subscription descriptors/registry
- State update and acknowledgement contracts

These types do not depend on ESPressio Event, Threads, Observable, Serializable, Serial, WiFi, Security, or ESP-NOW.

## Optional execution/observation

Remote State observation may use ESPressio Observable and existing ESPressio Thread/EventThread infrastructure, but those dependencies must not leak into the foundational contract/storage headers.

## Optional serialization/introspection

Human-readable names and serialization are opt-in and must not become required by the core runtime.

## Concrete transports

Concrete State transport adapters live with the transport that owns them:

- ESP-NOW State integration belongs in ESPressio ESP-Now.
- Serial State integration belongs in ESPressio Serial.

ESPressio State itself defines transport-neutral contracts only.

## Primitive boundary

State reports what is true now.
Event reports that something happened.
Command requests that something happen.

State must not absorb Event history or Command execution semantics.
