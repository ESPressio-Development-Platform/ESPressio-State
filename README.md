# ESPressio State

`ESPressio-State` provides deterministic latest-truth State primitives for the ESPressio platform. The `primitives_redesign` architecture owns canonical State values inside fixed `StateTypeRuntime<T>` cells, grants mutation through one move-only `StateOwner<T>`, uses immutable copy-out `StateSnapshot<T>` reads, and composes asynchronous observation through the common Threads capability model.

This branch is the architecture-reset implementation. The package version is intentionally unchanged while the redesign tranche is being integrated.

## State tiers

Every State definition is a compile-time Type with a non-zero `StateTypeId` and a static `CanonicalName`.

```cpp
struct TemperatureState final
    : ESPressio::State::State<TemperatureState, float> {
    static constexpr ESPressio::State::StateTypeId TypeId{0x1001};
    static constexpr std::string_view CanonicalName="App.State.Temperature";
};
```

Three tiers are available:

- `State<TDerived,TValue>` — local latest-truth State.
- `SerializableState<TDerived,TValue>` — local State whose bounded P3 schema can be serialized for diagnostics, persistence and dynamic read tooling.
- `TransmissibleState<TDerived,TValue>` — Serializable State plus distributed session/version/provenance semantics and a finite P2 `ConvergencePolicy` Type.

State values must satisfy deterministic bounded `StateStorageTraits`. Serializable and Transmissible values additionally require a bounded ESPressio-Serializable schema.

## Runtime, ownership and reads

A `State::Runtime<TypeConfiguration<...>>` freezes the Type set and all capacities before `Start()`. Runtime occupancy may change inside those fixed capacities, but topology does not resize after initialization.

```cpp
using namespace ESPressio;
namespace S=ESPressio::State;

Primitive::TypeDirectory<1> directory;
directory.Register<TemperatureState>();
directory.Initialize();

S::Runtime<S::TypeConfiguration<TemperatureState>> states;
auto owner=states.BindOwner<TemperatureState>();
states.Initialize(directory.View());
states.Start();

owner.Set(21.5f);

S::StateSnapshot<TemperatureState> snapshot{};
if(states.TryRead(snapshot)) {
    // snapshot.Value and snapshot.TruthTime are an immutable copy of one fact.
}
```

Exactly one owner capability may ever be bound for a State Type in one runtime incarnation. Binding the owner does not create a State fact. `Set()` prepares candidate storage first, compares against current truth, and then commits Value and qualified TruthTime atomically under the State synchronization boundary.

`StateComparison<TState>` controls authoritative owner equality. An equal/deadband candidate is a strict no-op: it does not update TruthTime, consume a compact version, wake observers, write persistence or create convergence work. Remote replica admission does not re-run the owner's semantic comparison; remote ordering follows the authenticated session/version contract.

`Set(value)` captures qualified System Clock time. `Set(value, truthTime)` accepts an explicitly supplied `Timing::QualifiedTime`. No mutable canonical pointer/reference is exposed.

## Local observation: TH10 capability

State mutation never invokes application code. `ObserverCapability<TStates...>` registers a frozen target-local observation index for each watched Type. A meaningful commit only sets the corresponding atomic pending bit and uses the owning Thread's common coalescing wake.

```cpp
class StateWorker final
    : public Threads::ThreadWith<S::ObserverCapability<TemperatureState>> {
    using Observer=S::ObserverCapability<TemperatureState>;
public:
    StateWorker() {
        GetCapability<S::ObserverCapabilityTag>()
            .OnChange(*this,&StateWorker::OnStateChanged);
    }
    ~StateWorker() override { (void)Shutdown(); }
private:
    void OnStateChanged(const S::StateChangeSet& changes) {
        if(changes.Contains<TemperatureState>()) {
            S::StateSnapshot<TemperatureState> current{};
            // Read current latest truth from the State Runtime here.
        }
    }
};
```

Multiple commits before service coalesce into one identity bit. No State value is queued with the notification. A change racing during the application callback republishes the bit for a later service quantum. Pause preserves pending identity; quiescence detaches the producer target.

## Serializable State and dynamic tooling

P1 `Primitive::TypeDirectory` remains immutable metadata. Its State family extension exposes tier, schema, bounded wire maxima, convergence-policy metadata and resource facts. It is not a mutable service locator.

`ReadDynamicState()` provides bounded local P3 serialization. `StateIntrospection<TStates...>` provides frozen static dispatch for tooling: metadata enumeration, bounded dynamic remote reads and retained remote-owner enumeration. Runtime current values are copied from canonical/replica storage; they are never stored inside the Type Directory.

There is deliberately no generic `SetByTypeId`. Dynamic Console/Web/Lua/GUI surfaces remain read-only unless application behavior is exposed through an explicitly authorized Command/API path.

## Persistent authoritative State

`StatePersistenceBinding<TState,Format>` binds a bounded P4 atomic-record store before Runtime initialization. For changed `Set()` operations, durable replacement succeeds before the canonical RAM/version commit. A persistence failure therefore leaves the previous RAM fact, TruthTime and compact version untouched and emits no observation or convergence work.

Restore preserves the original qualified TruthTime but starts a fresh runtime lineage at phase 0 / revision 1. Live remote sessions, trusted baselines and observer/network side effects are never restored.

## Transmissible State

A Transmissible State adds a bounded P2 convergence policy:

```cpp
struct PositionPolicy final {
    using PolicyCategory=Primitive::StateConvergencePolicyTag;
    using RequiredEvidence=Primitive::DestinationPrimitiveAdmission;
    using Supersession=Primitive::LatestAuthoritativeValue;
    using ExhaustionDisposition=Primitive::DormantNeedsConvergence;
    static constexpr std::uint64_t MaximumResidenceNanoseconds=1000000000ULL;
    static constexpr std::uint64_t MaximumAdapterAdmissionWaitNanoseconds=1000000ULL;
    static constexpr std::uint16_t MaximumAttempts=3;
    static constexpr std::uint64_t MinimumRetrySpacingNanoseconds=1000ULL;
    static constexpr std::uint64_t MaximumRetrySpacingNanoseconds=1000000ULL;
};
```

The application declares fixed remote-owner and source-subscriber capacities:

```cpp
using PositionConfig=S::TypeConfiguration<
    PositionState,
    S::MaximumRemoteOwners<4>,
    S::MaximumSubscribers<8>>;
```

Remote-owner storage is per Type, not a device x whole-contract matrix. Capacity exhaustion rejects new semantic occupancy; existing slots are never silently evicted.

### Subscription selectors

`SubscribeFrom<T>(device)` creates a SpecificDevice session. `SubscribeAny<T>()` asks the bound adapter to enumerate concrete owners locally and starts one normal concrete session per discovered device. `AnyDevice` is not a State wire broadcast.

Session tokens are process-wide monotonic non-zero tokens and are not reused. `Unsubscribe(handle, RetainLastKnown)` closes the session while retaining the last replica. `ReleaseReplica` frees the replica only after the local session is inactive.

`TryReadRemote<T>(owner,snapshot)` and `GetRemoteSessionStatus<T>(owner)` intentionally answer different questions. A retained last-known `{Value,TruthTime}` remains readable while a session is resynchronizing, closed or retained after unsubscribe. Reachability/link condition belongs to Mesh/Radio/adapters and is not synthesized as State availability, staleness or expiry.

### Baseline and compact ordering

A new session establishes a trusted full baseline before ordinary compact publications are accepted. If the source has no fact, the session becomes `ActiveNoBaseline`; the first later fact is sent as a full `BaselineSnapshot` and must be accepted before compact publication begins.

Within one owner RuntimeIncarnation, State uses a one-bit phase plus `uint16_t` revision. Full snapshot/resync control re-establishes trust when compact ordering cannot be proven. Runtime incarnation identity prevents reboot lineage from being mistaken for continuation.

### Exact V1 wire prefixes

Canonical State V1 uses little-endian fixed prefixes:

| Message representation | Prefix bytes |
|---|---:|
| Publication | 73 |
| Common session/control | 62 |
| Snapshot control | 78 |
| Acceptance control | 65 |

Payload bytes use bounded ESPressio-Serializable P3 formats. Native-object `memcpy` is not a State wire codec.

Every production remote admission also carries validated original-source provenance supplied by the adapter/security/session layer. State cross-checks semantic owner/requester identity before mutating session/replica state. An immediate relay is never automatically treated as the State owner.

## Adapter boundary and convergence

`StateTransportBinding<TState,Format>` is the frozen family-to-adapter seam. `Admit()` is only a bounded nonblocking transfer of semantic work ownership. The adapter must retain/copy anything it needs before returning `Accepted`; it must not wait for physical capacity, invoke application callbacks, or re-enter State mutation/lifecycle APIs from that call.

State owns semantic latest-truth/session/version decisions. Adapters own physical byte leases, routes, attempt counts, residence deadlines, retry spacing and lower-layer admission evidence.

When an adapter's finite campaign exhausts, it reports a `StateConvergenceHandle` back through Runtime service context. State stores only one bounded dormant `NeedsConvergence` bit for the exact still-current owner/requester/session/version/resync work. A stale feedback handle cannot dormant newer work. Pursuit is rearmed only by a newer authoritative commit, a relevant adapter availability transition, or explicit resync/continuity action. State has no periodic anti-entropy timer and no per-Type retry task.

Continuity-loss feedback is similarly correlated to exact identities, session and last accepted resync lineage. Last-known replica reads remain independent of recovery state.

## Runtime identity projection

`DeviceRuntimeIncarnationState` is an optional read-only Transmissible State owned by ESPressio-State. It projects the already-installed System `RuntimeIncarnationId` downward-to-upward without making System depend on State and exposes no application `StateOwner`.

## Resource accounting

`ESPressio_StateResources.hpp` reports target-specific compile-time footprints. The values are real `sizeof` results for the active compiler/target or explicit capacity multiplications; there is no hidden heap allowance.

```cpp
using Config=S::TypeConfiguration<
    PositionState,
    S::MaximumRemoteOwners<4>,
    S::MaximumSubscribers<8>>;
using Resources=S::StateRuntimeResourceAccounting<Config>;

constexpr auto position=S::StateDeploymentResources<Config>();
static_assert(position.RemoteOwnerCapacity==4);
static_assert(position.SubscriberCapacity==8);

using ObserverResources=S::StateObserverResourceAccounting<PositionState>;
```

Per-Type accounting reports the P1 directory-entry footprint, static State descriptor footprint, value/snapshot and `StateTypeRuntime` sizes, one observer-relation node, remote-owner/subscriber slot sizes and reserved-capacity products, complete remote table and selector object sizes, and optional DirectBinary persistence/transport binding costs. Runtime accounting reports `sizeof(Runtime<...>)`, State P1 entry reservation and process-wide session/resync token-authority static storage. Observer accounting reports the concrete capability size, registration-node bytes, pending bitmap, framework stack floor and external-storage requirement.

Component fields may overlap aggregate object sizes; do not add them unless the field explicitly represents a reserved/total value. Platform/provider control blocks outside these C++ objects require separate provider evidence.

## Lifecycle and concurrency boundaries

Initialization claims each configured Type and freezes bindings transactionally. Another Runtime cannot prepare or roll back a Type owned by the first. Start validates frozen observer/adapter topology. Shutdown closes new activity, takes the Runtime lifecycle gate exclusively, drains in-progress State operations, detaches borrowed bindings, closes sessions/selectors and retains canonical/remote snapshots for read-only post-shutdown inspection.

Remote ingress uses nonblocking lifecycle/table/token/selector acquisition and returns typed temporary-unavailable/capacity results instead of blocking an adapter receive context. Local authoritative `Set()` may use the normal short State synchronization path but never waits for transport convergence or application callbacks.

## Removed predecessor architecture

The V1 reset intentionally contains no compatibility aliases for the former architecture. These concepts are absent:

- `LocalStateRegistry`
- `StatePublisher`
- `RemoteStateManager`
- `RemoteStateObserverThread`
- `StateAvailability` / State-owned reachability or expiry
- `StateEpoch` / `StateRuntimeEpoch`
- `StateCodec`
- device x full-State-contract replica matrices
- Observable-based State publication callbacks
- native-layout State wire serialization

Tests contain configure-time guards preventing those headers/symbols from silently returning.

## Dependencies

ESPressio-State's direct `primitives_redesign` dependencies are:

- ESPressio-System
- ESPressio-Primitive
- ESPressio-Threads
- ESPressio-Timing
- ESPressio-Serializable
- ESPressio-Persistence

State has no canonical Observable dependency. Timing currently declares Observable for its own SystemClock observer API; raw host test harnesses therefore supply that transitive include separately.

## Examples

The `examples/` directory contains focused V1 examples:

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

The examples illustrate State-family semantics only. A production Transmissible deployment supplies a real adapter implementation in the later ESPressio-Adapters integration tranche; the State examples use small bounded mock bindings where transport behavior must be demonstrated.

## Validation

The branch CI builds the typed surface with C++17, `-fno-rtti`, warnings-as-errors host contracts, negative compile contracts and an ESP32 PlatformIO compile. Coverage includes owner uniqueness/move semantics, strict equal/deadband no-op, observer coalescing races, persistence-before-RAM, compact double wrap, session/baseline/resync ordering, exact V1 wire sizes, validated provenance, nonblocking family admission, shutdown/lifetime drain, continuity and convergence feedback, bounded local/remote dynamic tooling, remote non-callback behavior, predecessor eradication and resource-accounting formulas.
