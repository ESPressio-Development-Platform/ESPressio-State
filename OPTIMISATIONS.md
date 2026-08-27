# Optimisations

## 2026-08-27

- **#10** Added ESPressio-System as the platform-neutral memory abstraction dependency.
- **#10** Moved `RemoteStateManager` device/State repository storage to `ExternalPreferred` memory while retaining mutex/control state in the manager object.
- **#10** Moved remote device iteration snapshots off the caller stack into external-preferred temporary storage.
- **#10** Moved `ManagerObservable` shared allocation to external-preferred storage.
- **#10** Preserved forwarding/move semantics for accepted incoming State values and retained copies only where caller snapshots must remain independent of authoritative storage.
