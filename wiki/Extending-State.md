# Extending ESPressio State

Extensions must preserve State's defining semantic: **the newest authoritative truth matters; obsolete intermediate revisions are disposable.**

## New State definitions

Give each logical State a stable `StateTypeId`, even when several definitions share one payload type. Group fields into one value when they must be updated atomically.

## New codecs

Specialize `StateCodec<TDefinition>` for richer values rather than making State core depend on a general serialization framework. Keep encoding bounded and deterministic.

## New transports

Implement transport mapping in the transport-owning library. Preserve subscription enforcement, epoch/revision ordering, latest-only pending delivery and ACK semantics.

Do not leak native peer/address types into State.

## New observation layers

Core Observable callbacks must remain lightweight. If arbitrary application work needs another execution context, use a coalescing model that reads the latest repository value rather than queuing every obsolete revision.

## Repository changes

Preserve fixed capacity and transactional snapshot reads. Do not return unstable pointers into repository storage merely to avoid the intentional snapshot copy.

## Testing expectations

Cover:

- same-payload/different-State identity;
- old revision rejection;
- new epoch acceptance;
- compound-value atomicity;
- capacity exhaustion;
- unsolicited update rejection;
- pending revision supersession;
- stale ACK behaviour;
- availability independent of retained last-known State;
- resynchronisation scope;
- observer coalescing;
- codec malformed/oversized input.