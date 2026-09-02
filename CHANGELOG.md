# Changelog

All notable changes to ESPressio State are documented in this file.

## 0.1.0

Initial mutable development release.

### Added

- Transport-neutral 128-bit `DeviceIdentifier`.
- Compile-time `StateContract` and State definition traits.
- Fixed-capacity typed `RemoteStateManager`.
- Epoch/revision ordering for latest-value remote State semantics.
- Remote-device availability metadata.
- Typed publication and subscription registries.
- Latest-only State acknowledgement and supersession semantics.
- Explicit Subscribe/Unsubscribe acknowledgement protocol messages for reliable transport convergence.
- Duplicate Subscribe semantics that allow transports to re-send the current authoritative snapshot for resynchronisation.
- Observable lifecycle surfaces and optional coalesced application observer execution.
- Optional introspection and bounded diagnostic serialization.
- Host and ESP32 integration validation.
- Initial typed remote State example and documentation.
