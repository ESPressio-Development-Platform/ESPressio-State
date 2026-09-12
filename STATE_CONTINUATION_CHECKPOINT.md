# State continuation checkpoint — Tranche 5 closure — 2026-09-12

Scope: Primitive Platform redesign on `primitives_redesign`.

The user explicitly authorized continued implementation on the defined redesign branches. No version changes, tags, releases, force pushes or `main` integration are authorized by that continuation.

## Current State status

**State Tranche 5 is COMPLETE** against the locked S1-S8 + TH10 architecture and Section 26 completion gate.

The last implementation/test head before this documentation closure is:

`0d5c3d3a9b03ad3e6272bc8f0744e404ca1c53f7`

Actions run `34702911582`:

- host-contracts: SUCCESS
- esp32-typed-surface: SUCCESS
- host CTest: 37/37 PASS

The formal closure record is `STATE_TRANCHE_IMPLEMENTATION_REPORT.md` in this repository. This checkpoint intentionally summarizes only the current transferable state; earlier implementation history remains recoverable from Git history and the canonical Primitive architecture handoff.

## Completed State architecture

- Strong State Type/tier/version/result vocabulary.
- Process-lifetime typed canonical `StateTypeRuntime<T>` storage.
- Unique move-only `StateOwner<T>` authority.
- Qualified TruthTime and strict equal/deadband no-op semantics.
- Frozen `State::Runtime<TypeConfiguration<...>>` topology and P1 TypeDirectory integration.
- TH10 `ObserverCapability<TStates...>` using target-local pending bits + the common Thread Wake, with no producer application callback/value queue.
- Bounded P3 SerializableState integration; old StateCodec deleted.
- P4 persistence binding with durable-before-RAM commit and safe restore lineage.
- Fixed per-Type remote-owner and source-subscriber storage.
- SpecificDevice/AnyDevice selector semantics and process-wide non-reused session/resync tokens.
- Full baseline establishment, ActiveNoBaseline, compact trusted ordering, resync and delayed-ACK/continuity guards.
- Latest-truth supersession and source/resync service opportunities.
- Exact V1 73/62/78/65-byte wire prefixes.
- Validated original-source provenance and nonblocking family admission.
- Frozen adapter Validate/Admit/Wake/discovery seam.
- Correlated continuity feedback.
- Finite-campaign exhaustion feedback with one bounded dormant `NeedsConvergence` bit and explicit rearm causes only.
- P1/P3 local and remote read-only dynamic tooling.
- read-only `DeviceRuntimeIncarnationState` projection.
- deterministic resource accounting.
- rewritten README and twelve required compile-checked examples.

## Eradicated predecessor architecture

The following predecessor concepts are not compatibility surfaces and their headers are physically absent:

- LocalStateRegistry
- StatePublisher
- RemoteStateManager
- RemoteStateObserverThread
- StateAvailability / StateSourceReachability
- StateEpoch / StateRuntimeEpoch
- StateCodec
- old StateProtocol / StateSerialization / StateTransport / subscriber registry paths

All nine predecessor host tests were read and semantically classified before deletion. Their retained intent is covered by the redesigned suite; obsolete APIs were not repaired merely to keep old tests green.

Configure-time guards fail if the forbidden headers/symbols reappear.

## Build/dependency state

State's direct package/build dependencies are exactly:

- ESPressio-System
- ESPressio-Primitive
- ESPressio-Threads
- ESPressio-Timing
- ESPressio-Serializable
- ESPressio-Persistence

State has no canonical ESPressio-Observable dependency. The host test harness checks out/includes Observable solely because the current ESPressio-Timing manifest declares it for Timing's own SystemClock observer API.

State remains version `0.1.0`; the host configuration guard fails if it changes during this tranche.

## Final validation matrix

At `0d5c3d3a...`, the host suite contains 37 tests and all pass:

- 22 positive State contract/runtime tests
- 3 expected compile-failure tests
- 12 required V1 example syntax-compilation tests

Positive coverage includes local owner/runtime, compact wrap, comparison/deadband, TH10 observation, exact wire, persistence, remote sessions, provenance/admission, runtime handshake/resync, transport binding, selectors, dynamic reads, runtime-identity projection, shutdown/lifetime drain, initialization ownership, continuity feedback, convergence exhaustion/availability feedback, remote tooling/non-callback behavior and resource accounting.

ESP32/no-RTTI compilation also passes at the same head.

## Important later-platform gaps — do not misclassify as State regressions

1. Current ESP32 ExecutionProvider still lacks the required CreateJoinable/Join implementation. ESP32 compile does not prove live ThreadWith/TH10 execution on the concrete provider.
2. Current ESP32 storage/Preferences implementation does not establish the complete P4 hardware durability contract. State's abstract durable-before-RAM semantics are complete; hardware durability certification is later provider work.
3. Generic adapter finite-pursuit workers, byte leases, route ownership, attempt counts/deadlines/retry spacing and physical family ingress belong to Tranche 6 and later transport tranches. State must not absorb them.
4. Mesh/Radio/transport migrations remain dependency-ordered later work.

## Next authorized boundary

Do **not** begin generic ESPressio-Adapters implementation solely from the earlier broad implementation authorization if following the locked State completion protocol. The canonical handoff requires a State tranche report followed by an explicit request/authorization for Tranche 6.

When Tranche 6 is authorized, re-check live tips first, consume this completed State family seam as-is, and implement the generic adapter contracts without reviving State predecessor compatibility.
