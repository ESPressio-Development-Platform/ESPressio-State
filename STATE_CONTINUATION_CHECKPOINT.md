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
