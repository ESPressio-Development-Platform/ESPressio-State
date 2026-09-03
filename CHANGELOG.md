# Changelog

All notable changes to ESPressio State are documented in this file.

## 0.1.0

Active development baseline. Package version numbering is intentionally unchanged during the platform structural-realignment tranche.

### Added / realigned

- Canonical platform-wide `ESPressio::System::DeviceIdentifier` usage; State no longer owns or derives a second device identity.
- `StateAddress = DeviceIdentifier + StateTypeId` as the authoritative State identity.
- Compile-time `StateContract` and typed State definition traits.
- Application-owned local State binding through `LocalStateRegistry`, with non-owning typed views and registration-owned epoch/revision lineage.
- Unified `StatePublisher` lifecycle using `Bind`, `Unbind`, `NotifyChanged` and `Snapshot`; no duplicate local canonical value table or `LastPublished` shadow copy is retained.
- Retain/Discard unbinding semantics: Retain preserves epoch/revision lineage, while Discard causes the next bind to begin a new epoch.
- Explicit authoritative local availability lifecycle (`Available` and `Unavailable / SourceUnbound`) separate from value publication.
- Same-State publication notification serialization with bounded latest-fact coalescing for changes raised from inside an active publication callback.
- Bounded observer-registration capacities, independently configurable from data capacities, across publisher, remote manager, subscription registries and the optional deferred observer thread.
- Bounded typed `RemoteStateManager` replicas with epoch/revision ordering.
- Separate source-authoritative State availability and transport-derived source reachability, combined into effective remote availability for consumers.
- Per-State effective-availability transitions when source reachability changes.
- Typed local subscription and remote-subscriber registries.
- State protocol v2 with Publication, Availability, Subscribe, Unsubscribe, Resynchronize, SubscribeResult and UnsubscribeResult messages.
- Explicit authoritative Availability wire records; transport/Mesh-derived `SourceUnreachable` is not accepted as source-authoritative wire State.
- Latest-only outbound `StatePublicationTracker` semantics without baseline replica acknowledgements.
- Observable lifecycle surfaces and optional bounded/coalesced application observer execution through ESPressio Threads.
- Optional introspection and bounded diagnostic serialization exposing State availability and source reachability separately.
- Host and ESP32 integration validation.

### Removed / superseded

- State-owned MAC-specific `DeviceIdentifier` construction.
- Baseline State replica acknowledgement semantics.
- Generic State Disconnect protocol semantics.
- Callback-source `RegisterSource` publisher authority and its parallel publication/revision path.
