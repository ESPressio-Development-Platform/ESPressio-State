# Availability and Resynchronisation

Remote device liveness and last-known State are related but separate concepts.

Availability states include concepts such as:

```text
Unknown
Connected
Stale
Disconnected
ConnectionLost
```

## Last-known State

A device becoming stale or disconnected does not necessarily erase its last accepted State. The snapshot can remain readable together with availability metadata so application policy can decide whether that value remains useful.

This is preferable to silently converting a transport/liveness failure into loss of all domain knowledge.

## Resynchronisation

Resynchronisation requests current authoritative source-backed snapshots. It is scoped to subscribed State rather than indiscriminately dumping every State definition a device can publish.

Because publishers retain source accessors rather than a duplicate canonical repository, resynchronisation asks the domain owner for the current truth.

## Restart epochs

A newer origin epoch identifies a new publisher lifetime. This allows a restarted device to resume at a low revision without being rejected merely because the receiver remembers a high revision from the previous lifetime.

## Application policy

State infrastructure reports availability; the domain decides what availability means. A stale temperature reading, disconnected actuator status and lost camera pose may require very different application responses.