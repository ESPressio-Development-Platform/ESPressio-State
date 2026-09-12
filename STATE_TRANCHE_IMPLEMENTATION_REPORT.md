# ESPressio-State — Primitive Platform Redesign Tranche 5 Implementation Report

**Tranche:** State / S5-01 through S5-24  
**Branch:** `primitives_redesign`  
**Architecture authority:** Primitive Platform Redesign formal handoff, locked S1-S8 + TH10 contracts  
**Final implementation/test evidence head before this report:** `0d5c3d3a9b03ad3e6272bc8f0744e404ca1c53f7`  
**GitHub Actions evidence:** run `34702911582` — host contracts SUCCESS, ESP32/no-RTTI SUCCESS  
**Host result:** 37/37 tests passed  
**Version:** unchanged at `0.1.0`

## Completion statement

State Tranche 5 is COMPLETE against the locked State S1-S8/TH10 architecture and the Section 26 completion gate. The implementation is a clean architecture reset; predecessor Publisher/Manager/Availability/Epoch/Observable callback surfaces are not retained as compatibility APIs.

This report closes the State library tranche only. It does **not** claim that the later generic Adapters tranche, physical transport finite-pursuit implementation, Radio/Mesh migrations, ESP32 joinable execution provider, or concrete ESP32 P4 durability provider are complete.

## Implemented architecture

### S5-01 to S5-04 — State vocabulary, canonical ownership and versioning

- Strong `StateTypeId`, State tiers, immutable `StateSnapshot<T>`, typed result vocabularies and compact `StateVersion` are canonical.
- `StateTypeRuntime<T>` owns canonical bounded value storage and qualified TruthTime.
- Exactly one move-only `StateOwner<T>` may acquire mutation authority for one Type/runtime lineage.
- Binding an owner creates no State fact.
- `Set()` prepares bounded storage before the commit boundary and commits Value + TruthTime atomically.
- `StateComparison<T>` is part of authoritative owner admission. Equal/deadband candidates are strict no-ops: no TruthTime refresh, version burn, observer bit, persistence write, or convergence work.
- Remote replica admission deliberately does not re-run the owner's comparison/deadband; it follows trusted session/version ordering.
- Compact ordering supports both valid phase/revision zero wrap states; internal absence is represented separately and is not serialized.

### S5-05 — Runtime, frozen topology and P1 registration

- `State::Runtime<TypeConfiguration<...>>` owns fixed per-Type deployment capacity and freezes topology before Running.
- P1 `Primitive::TypeDirectory` is the immutable metadata directory; State does not create a parallel mutable registry.
- Initialization claims configured Type runtimes so another Runtime cannot prepare or roll back them.
- Failed/abandoned initialization detaches only the claimant's own borrowed bindings and releases only its own staged family claims.
- Runtime lifecycle uses a neutral System read/write gate. Admission/service use nonblocking shared acquisition; shutdown closes new activity, drains admitted work exclusively, drains canonical commits, then detaches borrowed bindings.

### S5-06 — TH10 State observation

- `ObserverCapability<TStates...>` is a Threads capability, not a State-specific Thread subclass.
- Registration is frozen before State Start.
- Producer commits publish only the target-local pending bit plus the owning Thread's common Wake.
- State mutation invokes no application callback and queues no State value.
- Repeated changes coalesce; a change racing during callback remains pending for a later quantum.
- Initial activation can synthesize one pending identity when current truth already exists.
- Pause preserves pending identity; quiescence detaches observation targets.

### S5-07 — SerializableState / P3

- `SerializableState` requires a bounded P3 schema and deterministic decode publication requirements.
- DirectBinary, CBOR and JSON bounded representations are supplied by ESPressio-Serializable.
- Family-specific `StateCodec` and native object-layout wire serialization were deleted.

### S5-08 — Persistence

- `StatePersistenceBinding<TState,Format>` binds a bounded P4 atomic-record store before Runtime initialization.
- Changed `Set()` performs durable replacement before RAM/version publication.
- Durable failure leaves RAM, TruthTime and compact version unchanged and emits no observer/convergence work.
- Restore preserves original TruthTime and begins a new runtime lineage at phase 0 / revision 1.
- Live remote sessions, trusted baselines, observer work and network work are not restored.
- Corrupt/incompatible committed records fail initialization closed.

### S5-09 to S5-10 — Fixed remote-owner and source-subscriber storage

- Remote-owner storage is fixed per State Type and capacity; there is no device x whole-contract replica matrix.
- Source subscriber storage is separately fixed per Type.
- Full capacity rejects new semantic occupancy; existing slots are never silently evicted.
- Last-known remote State remains a retained copy-out snapshot independent of session activity.

### S5-11 — Selectors and tokens

- `SpecificDevice` and `AnyDevice` selectors are implemented over frozen capacities.
- `AnyDevice` is local adapter discovery expansion into ordinary concrete sessions; it is not a State wire broadcast.
- Session and resync token authorities are process-wide, monotonic, non-zero and have no canonical reset API.
- Duplicate owner discovery is deduplicated before capacity checks.

### S5-12 to S5-15 — Baseline, resync and convergence

- Subscribe establishment commits destination state before protected acceptance replies.
- Source retries preserve the original SubscribeSnapshot/NoValue decision because SubscribeAccepted carries no version.
- `ActiveNoBaseline` is retained until the first later authoritative fact is transferred as a protected `BaselineSnapshot`.
- Newer truth committed during an older establishment/baseline transfer remains dirty and follows after acceptance.
- Trusted compact comparison rejects duplicate/older/inconsistent same-version traffic and requires resync when trust is lost.
- Resync uses fresh token correlation; old tokens cannot regain trust after replacement/continuity loss.
- Delayed ACKs cannot erase continuity loss, including across half-range ambiguity and full compact wrap.
- Exact duplicate source ResyncAccepted is idempotent and cannot restore trust lost later.
- One newest pending authoritative fact is retained per source session; older pending State work is superseded before irreversible adapter ownership transfer.

### Finite convergence feedback

- Family-to-adapter handoff transfers semantic work ownership only; State does not own routes, byte leases, attempt counts, retry spacing or residence deadlines.
- Outbound work exposes an immutable `StateConvergenceHandle` containing only semantic identity/session/version/resync correlation.
- Terminal adapter campaign exhaustion can be reported back through Runtime service context.
- State stores exactly one bounded dormant `NeedsConvergence` bit per affected fixed slot; stale exhaustion feedback cannot dormant newer work.
- Dormant work is rearmed only by a new authoritative commit, a relevant usable adapter availability transition, or explicit continuity/resync action.
- No State periodic anti-entropy timer, retry worker or per-Type task exists.

### Continuity feedback

- Continuity handles correlate Type, side, owner/requester runtime identities, session and accepted resync lineage.
- Only a trusted active baseline can be captured as continuity evidence.
- Continuity loss uses nonblocking lifecycle/table paths, rejects stale/replaced sessions, and preserves readable last-known snapshots.
- Remote-owner loss queues a fresh requester resync opportunity; source-subscriber loss queues bounded `ResyncRequired` work.

### S5-16 — State V1 wire contract

Canonical little-endian fixed prefixes are implemented and statically tested:

| Representation | Prefix bytes |
|---|---:|
| Publication | 73 |
| Common control | 62 |
| Snapshot control | 78 |
| Acceptance control | 65 |

The suite verifies offsets, identity fields, session/resync tokens, TruthTime, payload length, unsupported protocol handling, bounded payload round trips, and valid compact zero values after wrap.

### S5-17 — Provenance and remote admission

- Production ingress requires adapter-provided validated original-source runtime identity evidence.
- State cross-checks encoded semantic owner/requester identity before mutating replica/session state.
- Immediate relay identity is never substituted for semantic owner identity.
- Remote family admission uses nonblocking lifecycle/table/token/selector acquisition and returns typed retryable/capacity outcomes rather than blocking receive context.
- Required protected replies occur only after local semantic commit.

### S5-18 — Adapter-facing family seam

- `StateTransportBinding<TState,Format>` freezes Validate/Admit/Wake and optional owner-discovery member thunks.
- `Admit()` is contractually bounded/nonblocking ownership transfer and must retain/copy everything the adapter needs before returning Accepted.
- Adapter callbacks cannot wait for physical capacity, execute application callbacks, or re-enter State mutation/lifecycle APIs inline.
- Wake only marks/coalesces adapter service work; it does not service State inline.
- Borrowed adapter lifetime is protected by Runtime lifecycle drain.

### S5-19 — Dynamic tooling and runtime identity projection

- P1 State family descriptors expose tier, schema, bounded wire maxima, convergence-policy metadata and resource facts.
- `ReadDynamicState()` performs bounded local P3 read serialization.
- `StateIntrospection<TStates...>` provides frozen static dispatch for bounded remote reads and retained-owner enumeration without a mutable runtime registry.
- Dynamic tooling is read-only; no generic `SetByTypeId` exists.
- `DeviceRuntimeIncarnationState` projects System's installed RuntimeIncarnationId as a read-only State without creating an upward System dependency or exposing an application owner.

### S5-20 — Predecessor eradication

The following predecessor headers were physically deleted and are protected by configure-time absence/symbol guards:

- `ESPressio_LocalStateRegistry.hpp`
- `ESPressio_RemoteStateManager.hpp`
- `ESPressio_RemoteStateObserverThread.hpp`
- `ESPressio_StateAvailability.hpp`
- `ESPressio_StateCodec.hpp`
- `ESPressio_StateContract.hpp`
- `ESPressio_StateObservers.hpp`
- `ESPressio_StatePublisher.hpp`
- old `ESPressio_StateProtocol.hpp`
- `ESPressio_StateRuntimeEpoch.hpp`
- old `ESPressio_StateSerialization.hpp`
- `ESPressio_StateSubscriberRegistry.hpp`
- old `ESPressio_StateTransport.hpp`

No compatibility aliases exist for StatePublisher, LocalStateRegistry, RemoteStateManager, RemoteStateObserverThread, StateEpoch/StateRuntimeEpoch, StateAvailability/StateSourceReachability or StateCodec.

All nine predecessor tests were read/classified before deletion and replaced by target-architecture coverage. They were not mechanically repaired against obsolete APIs.

## S5-21 — Validation suite

At implementation evidence head `0d5c3d3a9b03ad3e6272bc8f0744e404ca1c53f7`, Actions run `34702911582` completed:

- `host-contracts`: SUCCESS
- `esp32-typed-surface`: SUCCESS

Host CTest: **37/37 passed**.

The 22 positive runtime/contract executables cover:

- Type contract
- compact version/wrap
- local runtime
- owner runtime
- TH10 observer
- exact V1 wire
- persistence
- remote sessions
- remote provenance/admission
- end-to-end remote runtime
- transport binding
- subscription selectors
- dynamic local read
- runtime identity projection
- shutdown/lifetime drain
- initialization ownership/rollback
- continuity feedback
- convergence exhaustion/availability feedback
- remote tooling
- remote non-callback observation behavior
- owner comparison policy vs remote acceptance
- deterministic resource accounting

Three negative compile tests pass by failing as required:

- zero StateTypeId
- invalid State storage traits
- unbounded SerializableState

Twelve required V1 examples are syntax-compiled with C++17, warnings-as-errors and `-fno-rtti`; all pass.

## S5-22 — Build/dependency cleanup

State direct package/build dependencies are:

- ESPressio-System
- ESPressio-Primitive
- ESPressio-Threads
- ESPressio-Timing
- ESPressio-Serializable
- ESPressio-Persistence

`library.json`, root ESP-IDF CMake and `component.mk` contain no direct ESPressio-Observable dependency. Tests enforce this. Timing currently declares Observable for its own SystemClock observer API, so the raw host harness checks it out/includes it only as Timing's transitive dependency. That does not make Observable a State dependency.

All workflow repository references use `primitives_redesign` for the coordinated dependencies. Package version remains `0.1.0` and is checked during host configuration.

## S5-23 — Documentation, examples and resource accounting

The predecessor README and `TypedRemoteState` example were replaced.

Required examples now present and compile-checked:

- `LocalOwnedState`
- `EqualValueNoOp`
- `ExplicitTruthTime`
- `ReadStateSnapshot`
- `StateObserverThreadComposition`
- `SerializableStateRead`
- `TransmissibleStateRuntime`
- `SubscribeFromDevice`
- `SubscribeAnyDevice`
- `RetainLastKnownAndReleaseReplica`
- `PersistentAuthoritativeState`
- `StateDynamicRead`

`ESPressio_StateResources.hpp` exposes target-specific deterministic accounting using real `sizeof` facts and explicit capacity products. It reports per-Type P1 entry cost, descriptor/value/snapshot/runtime sizes, observer relation cost, remote-owner/subscriber slot and reserved-capacity costs, table/selector sizes, optional DirectBinary persistence/record and transport binding costs, Runtime object/P1 reservation/process-token static storage, and TH10 capability/bitmap/registration/stack-floor facts. Overlapping component sizes are explicitly identified as non-additive.

## S5-24 — Tranche integration closure

The final closure evidence establishes all State completion-gate items available at this tranche boundary:

- S1-S8 + TH10 target semantics have executable coverage.
- All nine predecessor tests were classified before removal and replaced by target tests.
- Owner/observer/session/wire/persistence/provenance/convergence/tooling/resource suites pass.
- Exact 73/62/78/65 wire sizes and offsets pass.
- StateCodec/native State memcpy path is absent.
- StateEpoch/StateRuntimeEpoch are absent.
- RemoteStateObserverThread is absent.
- Canonical State Observable dependency is absent and guarded.
- README/examples describe the implemented V1 architecture; all required examples compile.
- Build/workflow dependencies target coordinated `primitives_redesign` branches.
- Version remains unchanged.
- Host suite and ESP32/no-RTTI compile pass at the exact implementation evidence head.

## Explicit non-claims / later-tranche blockers

These are not State Tranche 5 defects and are not silently declared complete:

1. **Generic Adapters finite pursuit:** State now exposes the locked family seam, wake, continuity and exhaustion/availability feedback. The real adapter worker/byte-lease/route/attempt/deadline implementation belongs to the later ESPressio-Adapters tranche.
2. **ESP32 joinable execution provider:** current ESP32 ExecutionProvider does not implement the required CreateJoinable/Join overrides; System base behavior is Unsupported. ESP32 compile therefore does not certify live TH10 Thread execution on that provider. This is later platform/provider work.
3. **ESP32 P4 durable storage:** current ESP32 Preferences/file provider does not establish the complete P4 durability capability contract. State's durable-before-RAM semantics are validated against the abstract atomic-record seam; physical hardware durability requires the later provider implementation/certification.
4. **Mesh/Radio/transports:** transport-family migrations and physical nonblocking ingress remain in their dependency-ordered later tranches.

## Handoff to Tranche 6

State is now ready to serve as the completed semantic family dependency for generic ESPressio-Adapters work. Do not reopen State's locked S1-S8/TH10 contracts for compatibility with predecessor adapters. Future adapter implementation must conform to `StateTransportBinding`, validated ingress provenance, bounded ownership transfer, finite P2 campaign/exhaustion feedback, and State's fixed semantic capacities.
