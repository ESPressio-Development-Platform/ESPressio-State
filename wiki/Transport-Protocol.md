# Transport Protocol

State owns compact transport-neutral messages for:

```text
Update
Acknowledgement
Subscribe
Unsubscribe
Resynchronize
Disconnect
```

The protocol identifies State through transport-neutral `DeviceIdentifier`, `StateTypeId`, epoch and revision rather than human-readable names.

## Transport responsibility

A concrete transport carries these messages and maps its peer/addressing model to `DeviceIdentifier`. Transport-specific MAC/socket/UART types must not leak into State core.

A transport should also enforce local subscription policy before applying incoming updates.

## Latest-only pending delivery

Transport reliability retains at most the newest pending revision for a destination/State identity. Retransmission must not reconstruct a historical queue of superseded State.

## Codec boundary

`StateCodec<TDefinition>` owns conversion between a State definition's typed `Value` and its bounded payload representation.

Suitable trivially-copyable values can use the allocation-free default path. Richer State definitions can specialize the codec.

A custom codec must preserve:

- bounded output/input expectations;
- deterministic failure handling;
- complete-value atomicity;
- stable interpretation for the State contract's compatibility lifetime.

## Serialization independence

The State transport protocol does not require ESPressio Serializable, JSON or CBOR. Diagnostic serialization is a separate optional layer and must not silently become a second authoritative State wire protocol.