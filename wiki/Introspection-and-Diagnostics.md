# Introspection and Diagnostics

Human-readable discovery and diagnostic serialization are optional layers, deliberately excluded from the core State path.

## Introspection

Select explicitly:

```cpp
#include <ESPressio_StateIntrospection.hpp>
```

Definitions may expose a diagnostic name:

```cpp
static constexpr const char* Name = "gyroscope.front";
```

`Name` is never identity. `StateTypeId` remains authoritative.

Introspection supports ID/name lookup and typed enumeration of one State, one device or the remote repository without changing core storage semantics.

## Diagnostic serialization

Select explicitly:

```cpp
#include <ESPressio_StateSerialization.hpp>
```

Diagnostic records contain bounded representations of device identity, State type, optional name, epoch, revision, availability and encoded payload.

The payload uses the authoritative `StateCodec<TDefinition>` rather than introducing a second encoding contract.

## Compile-time exclusion

The optional layers can be compiled out through their feature macros. Applications that do not need human-readable tooling should not pay for it.

## No mandatory Serializable dependency

These diagnostic facilities do not require ESPressio Serializable, ArduinoJson, JSON or CBOR. Serial/Web/debug adapters can consume the bounded diagnostic records at their own layer.