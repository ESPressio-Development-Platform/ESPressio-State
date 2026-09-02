# Platform Abstractions Audit Trail

This file records State changes made during the platform-abstraction tranche tracked by issue #13.

## 2026-08-27

### Audit result
- Core State types, codecs, contracts, publishers, managers and protocol code contain no required ESP32/Arduino/ESP-IDF/FreeRTOS runtime dependency.
- `RemoteStateObserverThread` delegates execution and wake-up behaviour to ESPressio-Threads `PrecisionThread`; it does not own a native task/runtime primitive.
- `DeviceIdentifier::FromMacAddress` only transforms caller-supplied address bytes and does not query a platform network interface.

### Package metadata
- Removed the unnecessary Arduino/ESP32-only package restriction from `library.json`.
- State now advertises its core as framework- and platform-neutral while retaining optional execution integrations through explicitly selected headers/dependencies.

## Boundary rule

State owns state definition, publication, observation, replication and remote-state semantics. Hardware/runtime execution remains delegated to lower ESPressio layers.
