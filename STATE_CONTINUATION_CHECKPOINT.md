# State continuation checkpoint — 2026-09-12

Scope: continue the locked Primitive Platform redesign on `primitives_redesign`.
The current user explicitly authorizes continuation without another permission request.
No version changes, tags, releases, force pushes, or main integration are authorized.

## Baseline reconciliation

Revision 102 recorded State `f66a7af1683947f64d92d1d7aa07909333295315`.
The live starting tip was `93f65872edba91c7ee91663ab717063b1beb8ea6`, with
49 later commits and successful Actions run `34691832689`.
Those commits introduce TH10 observation, P3/V1 codecs, persistence, remote tables,
session/convergence primitives, validated-source decoding, transport binding and selectors.
They are preserved as the implementation base, not reimplemented from Revision 102.

Command matches `b403ca1bb20162c2c88af1ba3506aeb4958ec7e8` and Primitive matches
`2af5ada3dadeff58804468e4803935a78834edc9`. Serializable advanced from
`0c248a0f4d45bcbe25c57cb66a26d61f0d361d6d` to
`2a0dff001cae91365d90f20a382036bd878f53e4` (older-GCC schema fingerprint support).

## Repairs in this checkpoint

- Close the exact local subscription and update selector occupancy before offering a remote
  Unsubscribe notification. Backpressure cannot leave local admission open. Copy the learned
  owner identity before releasing replica storage; never fabricate an incarnation for a
  still-establishing session. An old handle cannot close a replacement session.
- Share session and resync token allocation across disjoint Runtime Type packs. Both sequences
  retain process lifetime, reject exhaustion, and expose no reset through the canonical Runtime.
  The shared mutex is resolved during initialization.
- Reject ForgetRemote for an active session; only inactive retained replicas can be forgotten.
  Failed subscription admission explicitly closes its provisional session before releasing storage.
- Deduplicate owner discovery before checking the capacity limit, so repeated discovery of an
  existing owner does not consume capacity or falsely reject an otherwise bounded AnyDevice set.

These implement existing Section 7.50/S3.2 and Section 26.16–26.17 contracts. No locked
architecture is changed, and no compatibility shim is added.

## Validation

The existing 11 active host executables were compiled directly with GCC, C++17,
`-Wall -Wextra -Werror -fno-rtti -UNDEBUG`, using exact sibling dependency checkouts.
All passed before changes. The expanded suite and three expected compile-failure fixtures
passed after the initial repairs; the final owner-identity closure refinement is revalidated
before this checkpoint is pushed. CMake is not installed in this local runtime.

The expanded subscription test checks local closure from inside the adapter admission hook,
late publication rejection, retained Value/TruthTime, stale-handle rejection, release behavior,
and duplicate discovery at capacity. The session test checks active-forget rejection and token
continuity across two disjoint Runtime compositions. Replaying the regression tests against
the predecessor source fails on the local-closure and active-forget assertions as expected.
Exact final CI evidence is recorded in the Primitive handoff and canonical architecture document.

## Remaining work — not a tranche completion report

The full 1.34 MB handoff read and exhaustive mandatory repository-hotspot re-audit are not
complete in this checkpoint. The continuation performed branch reconciliation and targeted
State/provider source review. Do not represent that as completion of the Section 0 gate.
Finish that gate before broader redesign implementation.

State still lacks a proven end-to-end family ingress -> session mutation -> protected reply /
latest-truth emission path. `DecodeValidatedStateIngress` is a validation/decode helper;
its Accepted result is not proof that a destination session has committed the message.
Current table-level helpers do not close the full S5-12/S5-14/S5-15 handshake/resync/convergence
obligations. Source acknowledgement matching, truth advancing during baseline establishment,
finite campaign exhaustion, continuity invalidation and nonblocking ingress all require
completion and end-to-end tests before State is promoted.

P1 read-only dynamic dispatch and DeviceRuntimeIncarnationState remain open, as do legacy
eradication, classification/replacement of all nine predecessor tests, full resource accounting,
README/examples migration, and S5-21–S5-24 closure. The predecessor source files remain for
their scheduled replacement boundary; their presence grants no compatibility exception.

Adapters and RadioAdapters remain bootstrap-only. Subsequent tranches retain the original
dependency order and completion gates. Only pushed commits count as recoverable progress.

## 2026-09-12 handshake and P1 continuation

Starting from validated published tip `e1acd06a7b63a62fb42fff84c1bc243d3baf2648`,
this checkpoint adds the Type-specific family admission path for subscribe establishment,
late first baseline, publications, exact acknowledgements, resynchronization and unsubscribe.
Destination mutation precedes each protected reply. Duplicate baseline and resync snapshots
are re-acknowledged, and the source keeps the newest truth dirty when it advances during an
older transfer. `ServiceLatest` gives the adapter service context a bounded semantic-message
seam and cannot clear a newer dirty version when an older offer completes.

The checkpoint also adds P1 dynamic read dispatch for all three bounded serialization formats
and the read-only `DeviceRuntimeIncarnationState`. Runtime initialization projects System's
installed process incarnation with qualified time, version 1 and no public owner capability.

Fourteen active host executables and all three expected compile-failure fixtures pass with GCC,
C++17, `-Wall -Wextra -Werror -fno-rtti -UNDEBUG`. CI at the published exact head remains the
authoritative host and ESP32 evidence.

This is still not State tranche completion. The full architecture document read remains
incomplete: original Revision 102 lines 1–3250 and Section 26 lines 10317–12164 are the only
fully counted ranges. A later attempt to read lines 3251–5500 was truncated and is not counted.
The exhaustive Section 0 source audit, adapter wake/continuity seam, finite campaigns,
legacy replacement/eradication, documentation/examples/resources and final gates remain open.

## 2026-09-12 correlated resync and first-baseline repairs

The published handshake/P1 base is `1b51d0c44d59643beac8996f7a412795a9be27d9`;
local sibling `d1e5b8b245e14729a02e9dfc50004d8df897dcc1` has the identical tree and is
not an additional published checkpoint. A concurrent continuation's changes were preserved.

Focused regression review found three defects in that base. Wire ResyncRequired now checks
the exact owner incarnation, session and active state in the same table transaction that
allocates its fresh token; delayed controls cannot invalidate replacement or closed sessions.
The first truth committed during SubscribeNoValue establishment remains dirty for a protected
BaselineSnapshot. Ordinary baseline/publication acceptance cannot restore trust during resync;
only the token-correlated ResyncAccepted path may do so.

All three regressions were observed failing before their corresponding source repairs.
The 14 active host tests passed during this checkpoint; remote_runtime, remote_sessions and
subscriptions were rebuilt and passed after the final correlated-ACK guard. Compiler flags
remain C++17, warnings as errors and no RTTI. Exact published SHA and CI follow in the
canonical and Primitive handoffs. No complete nonblocking-ingress or tranche claim is made.

The uninterrupted foreground continuation has now completed the remaining architecture
document reading, including Sections 8–25, 27–32 and the historical revision ledger; truncated
ranges were reread in smaller chunks. This supersedes the earlier partial reading counters.
The exhaustive source-hotspot audit remains open. Additional inspected code includes current
Command lane/admission implementation, Mesh receiver/worker, all four MeshAdapters submission
paths, RadioWorker and RadioTransport ingress/fragmentation. These still expose the documented
later-tranche migration gaps. Continue the source audit and State completion under existing
authorization; do not repeat the entire document read or request implementation permission.

2026-09-12T13:16:42.322406+00:00

## STATUS UPDATE: mandatory context audit complete; retry-safe State controls in progress

The foreground continuation completed the Section 1 current-state data-flow and ownership
review, following the full document read. It inspected the remaining Thread root lifecycle/
common-wake/Precision implementation and tests, Task substrate shutdown tests, Event lane/
inbox/outbound implementation, State predecessor sources and all nine predecessor tests,
Serializable bounded graph/decode, System identity/synchronization, Persistence atomic records,
Mesh receiver/broadcast/worker, MeshAdapters ownership, Radio ingress/egress/clock, Observable,
Logging/Security integration and ESP32 execution/radio/storage providers. This establishes the
required current-state model; it does not certify unimplemented behavior or hardware behavior.
The earlier source-audit-open counters are superseded. Further source inspection should target
implementation work, not restart the broad audit.

Current data flow: local Event captures origin before pool/lane admission; its one T1 lane
fans out retained leases, and selected Thread capabilities release FIFO quota before callback.
Command reserves fixed request/execution/response state, invokes handlers only on T1 lanes
after durable Started, and routes responses through a shared TaskExecutor to selected TH16
callbacks. State Set commits persistence before canonical RAM and observer bits; current
remote ingress runs synchronously in the adapter's family-service caller, with bounded table
state and borrowed outgoing semantic messages. Mesh delivers borrowed bytes synchronously;
its old Event adapter must own a packet before returning and is not compatible with the new
Event family surface. RadioWorker drains provider ingress, RadioTransport reassembles into
fixed records and borrows completed bytes through callbacks before reset; outgoing fragmentation
still dynamically sizes a per-send frame. RadioControlWorker services synchronization;
Mesh selects the reference, while the old Radio exchange still uses current-clock-minus-elapsed
capture reconstruction and fixed cadence. These later migrations remain explicit work.

A material provider gap is confirmed: ESP32Platform::Initialize installs ExecutionProvider,
which implements ordinary Create/Destroy but does not override CreateJoinable/Join. The base
System interface returns Unsupported, so current no-RTTI compile probes do not establish
working Task/Thread runtime execution with that concrete provider. Its cooperative execution
implementation is required before final platform completion. ESP32 file/Preferences storage
also does not advertise the complete P4 durability capabilities; do not claim hardware durability.

This next State change stores one fixed control snapshot per source subscriber. Subscribe
retries preserve the original snapshot or NoValue result because SubscribeAccepted has no
version. Resync retries preserve the snapshot associated with their token; a bounded token
high-water rejects older resync requests. Older same-runtime SubscribeRequest tokens cannot
replace a newer live source session. Set records LatestVersion even during establishment or
resync; acceptance and latest-transfer preparation cannot overwrite a newer table fact with
a stale canonical read. Tests exercise initial and resync retry across Set, NoValue retries,
older session/token rejection, and deterministic read/commit/completion interleavings.
State remains incomplete: nonblocking transactional admission, wake/continuity integration,
finite pursuit scheduling/exhaustion, remote observation/tooling, legacy/docs/resource/final
closure are still required before Adapters. Existing implementation authorization remains active.


## 2026-09-12T13:27:02.214627+00:00 — First baseline retention and source resync opportunities

Parent: 53aeed239c129ab372eda88ffa88efa86049aa54, CI 34696024641 host-contracts and esp32-typed-surface SUCCESS.
This change reuses the fixed subscriber ControlSnapshot for the first baseline after NoValue;
new truth cannot replace it before acceptance, and remains dirty for publication after acceptance.
The regression failed on the predecessor algorithm and passes with the repair.
ResyncRequired now has a bounded pending source control opportunity offered by ServiceLatest;
rejection retains it, successful admission transfers it once, and newer truth rearms it.
An older completion cannot clear a newer pending opportunity. Runtime tests verify identity,
session and absence of snapshot payload on that control. Fourteen active host executables
were rebuilt and passed; remote_runtime was rebuilt and passed again after the final runtime
control assertions. Full campaign retry/exhaustion and wake integration are still incomplete.

The user requires a continuously updated transferable living handoff even on usage-limit exit.
Canonical handoff Revision 106 records the completed document/current-state audit, published
heads, provider gaps and working changes. Continue authorized work without checkpoint stops.
Next: pending-handshake continuity invalidation, nonblocking ingress/lifetime, wake/campaigns,
remote observation/tooling, legacy eradication, docs/resources and State final closure.


## 2026-09-12T13:32:44.976724+00:00 — Delayed control continuity and second compact wrap

Parent c75d6e380efc94e8363e1bcb28118e2e6fcd6c9e, tree
8419ed3ce66b8ea8e554c2d1f58fe324c9c24051. CI 34696478084 has both host-contracts
and esp32-typed-surface SUCCESS. First-baseline retention and source ResyncRequired opportunities
are published and validated; finite worker pursuit remains pending.

Current change latches continuity loss during initial/first-baseline/resync acceptance waits.
Half-range loss and best-effort wrap cannot be undone by a delayed ACK, even when a full compact
cycle makes the bits repeat. A fresh resync resets the control lineage. The initial delayed-ACK
regression failed against the predecessor and passes after repair.

StateVersion now distinguishes internal absence from the valid phase-0/revision-0 value reached
after the second wrap. Presence is not serialized; all V1 sizes/offsets remain unchanged. The old
wire test incorrectly prohibited that valid value and is corrected with publication, snapshot,
and acceptance round trips. A real 131072-fact owner test, replica duplicate/next publication,
and equal-value no-op cover the runtime boundary. The second-wrap regression failed before repair.
Fourteen active host executables passed after the core changes; the expanded remote_sessions
runtime wrap assertions are rebuilt separately before publication. This does not certify the
remaining nonblocking/lifetime, wake/campaign, remote-observer/tooling or final State gates.


## 2026-09-12T13:40:13.168705+00:00 — Nonblocking family admission lock path

Parent c0dc8fdae4b6eed00b46fb1cbeb39c23c79e8a30, tree
9fe3a8ef347c0daf119ad354979d44c52696e828. CI 34696752222 host-contracts and
esp32-typed-surface both SUCCESS. Primitive handoff is now
7b90e0db8c3a1e86d2284a20af2558d876bdce62 (Revision 107).

This change routes every framework mutex on Runtime::AdmitRemote through one try-lock attempt.
Table and selector synchronization is resolved during Initialize alongside the existing token
and Type runtime storage. Busy maps to TemporarilyUnavailable, separately from NoValue and
TokenExhausted. Nonblocking canonical capture and frozen outbound binding acquisition preserve
the established local-commit-before-reply retry semantics. Local helper paths retain blocking
access where allowed. Adapter Admit is explicitly required to perform bounded nonblocking
ownership transfer without application callbacks or waiting for capacity.

The remote_runtime fixture installs an instrumented synchronization provider that fails any
blocking lock or lazy mutex creation during admission. It failed on the predecessor. The new
path passes all exercised handshake/publication/resync controls. Injected contention at all four
source-subscribe lock points, post-replica-commit ACK transfer, process token allocation, and
both selector-rejection locks returns retryable status and preserves the appropriate state.
All 14 active host executables were rebuilt and passed with C++17 warnings-as-errors and no RTTI.
This proves the framework lock path under its bounded storage/adapter contracts, not arbitrary
user adapter behavior, hardware latency or full shutdown/lifetime safety.

Next: close admission and service activity before shutdown, drain admitted users, detach borrowed
convergence lifetime safely; then frozen wake/continuity callbacks, finite campaign accounting,
remote observer/tooling and final State eradication/resource/docs gates. Plain Runtime running
state and shutdown concurrency are still open in this checkpoint. The user's living-handoff
requirement remains active; save exact current published and uncommitted state before any exit.


## 2026-09-12T13:51:56.063139+00:00 — Runtime quiescence and borrowed binding teardown

Parent e5b0b0956e4eeb8301b8a07ec760e55abf342320, tree
043de64b149c83694ce8d6df000d3c58652ef1de. CI 34697102823 host-contracts and
esp32-typed-surface both SUCCESS. Canonical living handoff is Revision 108.

A neutral System read/write lifecycle gate is resolved during Initialize. Admission and latest
service take nonblocking shared leases; local mutating operations participate in the same gate.
Atomic closing/running flags reject new work before shutdown waits for existing operations.
Shutdown requests Type stop, drains active operations exclusively, drains canonical owner commits,
detaches transport/persistence/convergence bindings, closes sessions and selectors, and retains
readable canonical/remote snapshots. An initialized Runtime destructor performs the same cleanup.
Start cannot revive a stopped Type. Shutdown is external lifecycle control; invoking it from
inside a Runtime operation's bound callback would attempt to drain itself and is prohibited.

The new shutdown host fixture failed on the predecessor because shutdown returned before an
owner commit completed. It now verifies commit drain, adapter-service drain, rejection after
closure, retained snapshots, binding detach and no restart. The admission instrumentation also
covers the lifecycle try-lock and forbids lazy read/write-lock allocation. All 15 active host
executables passed. After the final early-closure guards, shutdown/remote_runtime/subscriptions
were rebuilt and passed again. CI for this new checkpoint is required after publication.

Still open: failed/partial initialization ownership and rollback audit (avoid rolling back a
Type prepared by another Runtime; abandoned convergence staging needs explicit cleanup); exact
source duplicate ResyncAccepted handling; adapter wake/continuity and finite convergence pursuit;
remote observations and remote dynamic tooling; legacy/resource/docs/final gates. Existing
ESP32 cooperative execution and P4 durability gaps remain later platform migration work.


## 2026-09-12T13:58:55.721475+00:00 — Initialization claims and owned rollback

Parent 4b6c120ab142cffb8cfa080202294fbaba31a49f, tree
a9119559f37affe558bebe4512ea4d0db1912394. CI 34697654142 host-contracts and
esp32-typed-surface both SUCCESS. Canonical living handoff is Revision 109.

Each Type now records its staging family owner. BindOwner/BindTransport/BindPersistence and
Initialize claim the Type; another Runtime cannot prepare or roll back that configuration.
Rollback checks the claimant. Failed initialization retains its own staged configuration for a
corrected retry; destruction of an uninitialized claimant detaches only its own borrowed
bindings and releases its claim. Running teardown clears the claim after drain. Owner capability
non-rebinding remains process-lifetime and is not reset by abandoned staging.

The new initialization fixture failed on predecessor source when a conflicting Runtime rolled
back the first Runtime's Prepared Type. It now verifies that the first Runtime can Start/Set,
that partial claims are released on abandonment, and that failed transport/convergence staging
can be replaced without retaining the old Runtime address. All 16 active host executables were
rebuilt and passed. The CMake list includes initialization. Current CI is required after publish.

Next: exact duplicate source resync acceptance, frozen wake/continuity seam and adapter-reported
finite convergence pursuit/exhaustion, remote observer/read tooling, legacy/resources/docs/final
State closure. Keep semantic campaign state in State and adapter retry ownership on its locked
side of the boundary; do not add per-Type Tasks or periodic anti-entropy polling.


## 2026-09-12T14:02:45.996460+00:00 — Idempotent source resync acceptance

Parent c94e9ecbe2c9c9de3a5d0e3c9a9bf018e4259db8, tree
f6788a55b27f071b9a84706920c38f6406ae7049; CI 34697949834 host-contracts and
esp32-typed-surface SUCCESS. Sixteen active host executables passed for that parent.
This change retains one last accepted source resync token/version per subscriber. Exact repeated
ResyncAccepted is AlreadyAccepted without mutation, including after subsequent continuity loss;
it cannot restore trust or replace a newer transaction. The regression failed before repair.
Focused remote_runtime, remote_sessions and subscriptions rebuilds pass. Remaining wake/campaign,
remote observer/tooling, legacy/resources/docs/final gates stay open. Canonical living handoff
Revision 110 records this work and must advance after publication/CI.


## 2026-09-12T14:10:10.823049+00:00 — Frozen adapter work wake

Parent 990b077952753bfbc87055620ca5bb31791dda4e, tree
8748b7f6aee7e27ce5fa36a8ae28a4fc23c2604a; CI 34698148784 host-contracts and
esp32-typed-surface SUCCESS. Exact duplicate source resync acceptance is now published/verified.

StateTransportBinding now requires an explicit frozen Wake member thunk in Initialize and
InitializeWithDiscovery. Wake only marks/coalesces adapter service work; it cannot copy State,
wait, service inline, invoke application code or reenter State. Validate must establish the
signal/service lifetime before Start. Changed Set publishes dirty metadata then signals work;
equal Set remains a strict no-op. Successful source baseline/establishment/resync acceptance and
explicit source resync trigger also wake service. Runtime lifecycle/canonical drain protects
the borrowed signal target through shutdown. This is a work signal, not a new Type task/queue.

The wake regression failed before implementation. All 16 host executables pass, covering no
inline publication, no wake on equal Set or stopped Set, and wakes after source acceptance and
resync request. All fixture adapters supply explicit wake methods. The unsubscribe probe now
observes local closure read-only inside Admit and attempts the late mutation after Admit returns;
nested mutations are outside the nonreentrant adapter admission contract and are not portable
with ESP32's serialized read/write lifecycle gate.

Next: exact correlated adapter continuity-loss and finite-campaign exhaustion feedback with
bounded dormant NeedsConvergence metadata; explicit availability/new-commit/resync rearming;
remote observation/read tooling; legacy/resources/docs/final closure. The adapter's real finite
pursuit implementation remains in its locked later tranche. Do not claim the wake hook alone
completes convergence scheduling or State.


## 2026-09-12T14:26:28.911442+00:00 — Correlated adapter continuity loss

Parent bc05536e2cafd6634914d39ba981d2cd43701b76, tree
e63191d4bf5ec534d659c7cc12d22358b8b55d25; CI 34698546876 host-contracts and
esp32-typed-surface SUCCESS. Primitive handoff is now
79a1315eaade1293f4f0823b159ee599208abafd through Revision 111.

StateContinuityHandle identifies Type, side, exact owner/requester incarnations, session and
last accepted resync token. Capture/ReportContinuityLoss use nonblocking lifecycle/table paths.
Only a trusted compact baseline has a capturable handle. Ordinary publications preserve the
lineage, while a completed resync changes it. Stale/replacement/closed feedback is rejected;
already recovering sessions report Duplicate without cancelling a newer resync. Last-known
snapshots stay readable. Source loss queues ResyncRequired; requester loss queues a bounded
pending ResyncRequest opportunity and wakes the frozen adapter service.

ServiceLatest handles queued requester resync before requiring a local canonical fact, enabling
remote-only deployments. Each failed ownership handoff retries with a fresh token. Completion
is correlated by owner/session/token, so it cannot overwrite newer recovery or closed state.
Accepted ownership leaves no periodic State retry. This adds fixed metadata, not another task.

All 17 active host executables pass, including the new continuity_feedback fixture. Coverage
includes source and requester lineage, ordinary advancement vs fresh resync, contention without
mutation, duplicate feedback, fresh token after failed admission, retained reads, invalid
Type/side, closed sessions and service without a local owner/fact. CMake includes the new test.
CI for this checkpoint must be checked after publication. Exhaustion/availability feedback and
finite campaign metadata, remote observations/tooling, legacy/resources/docs/final gates remain.
