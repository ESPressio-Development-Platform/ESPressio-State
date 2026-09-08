#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>

#include <ESPressio_Memory.hpp>

#include "ESPressio_DeviceIdentifier.hpp"
#include "ESPressio_StateContract.hpp"

namespace ESPressio {
namespace State {

/// <summary>Transport metadata that uniquely identifies one authoritative State revision.</summary>
/**
 * ESPressio Memory Audit
 * Members:
 * - Origin (DeviceIdentifier): 16 bytes [0 bytes dynamic allocation]
 * - TypeId (StateTypeId): 8 bytes [0 bytes dynamic allocation]
 * - Epoch (StateEpoch): 4 bytes [0 bytes dynamic allocation]
 * - Revision (StateRevision): 8 bytes [0 bytes dynamic allocation]
 * Total Memory: 36 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
struct StateUpdateHeader {
    DeviceIdentifier Origin{};
    StateTypeId TypeId = 0;
    StateEpoch Epoch = 0;
    StateRevision Revision = 0;
};

/// <summary>Combines State transport metadata with one strongly typed authoritative value.</summary>
/**
 * ESPressio Memory Audit
 * Members:
 * - Header (StateUpdateHeader): 36 bytes [0 bytes dynamic allocation]
 * - Value (TValue): sizeof(TValue) [0 bytes dynamic allocation]
 * Total Memory: 36 bytes known/aligned storage + sizeof(TValue) [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * Confidence: low; compile-time sizeof on the concrete target remains authoritative for ABI-sensitive/opaque members.
 * End ESPressio Memory Audit
 */
template<typename TValue>
struct StateUpdate final {
    StateUpdateHeader Header{};
    TValue Value{};
};

/// <summary>Retains only the latest committed publication for one State publication target.</summary>
/**
 * ESPressio Memory Audit
 * Members:
 * - Pending (bool): 1 bytes [0 bytes dynamic allocation]
 * - Epoch (StateEpoch): 4 bytes [0 bytes dynamic allocation]
 * - Revision (StateRevision): 8 bytes [0 bytes dynamic allocation]
 * - Value (TValue): sizeof(TValue) [0 bytes dynamic allocation]
 * Total Memory: 16 bytes known/aligned storage + sizeof(TValue) [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * Confidence: low; compile-time sizeof on the concrete target remains authoritative for ABI-sensitive/opaque members.
 * End ESPressio Memory Audit
 */
template<typename TValue>
struct PendingStateUpdate final {
    bool Pending = false;
    StateEpoch Epoch = 0;
    StateRevision Revision = 0;
    TValue Value{};

private:
    template<typename TValueArgument>
    bool ReplaceValue(StateEpoch epoch, StateRevision revision, TValueArgument&& value) {
        if (revision == 0) return false;
        if (Pending && epoch < Epoch) return false;
        if (Pending && epoch == Epoch && revision <= Revision) return false;
        Epoch = epoch;
        Revision = revision;
        Value = std::forward<TValueArgument>(value);
        Pending = true;
        return true;
    }

public:
    bool Replace(StateEpoch epoch, StateRevision revision, const TValue& value) {
        return ReplaceValue(epoch, revision, value);
    }
    bool Replace(StateEpoch epoch, StateRevision revision, TValue&& value) {
        return ReplaceValue(epoch, revision, std::move(value));
    }
    void Clear() noexcept { Pending = false; }
};

/// <summary>
/// Coalesces outbound publication work for one destination to the latest State revision.
/// </summary>
/// <remarks>
/// The tracker deliberately has no acknowledgement lifecycle. State transport is
/// asynchronous latest-fact replication; transport owners consume the retained
/// latest publication and may replace it with a newer revision before delivery.
/// </remarks>
/**
 * ESPressio Memory Audit
 * Members:
 * - _destination (DeviceIdentifier): 16 bytes [0 bytes dynamic allocation]
 * - _pending (PendingStateUpdate<Value>): 20 bytes [0 bytes dynamic allocation]
 * Total Memory: 36 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
template<typename TDefinition>
class StatePublicationTracker final {
public:
    using Value = StateValueType<TDefinition>;

private:
    DeviceIdentifier _destination{};
    PendingStateUpdate<Value> _pending{};

public:
    explicit StatePublicationTracker(const DeviceIdentifier& destination = DeviceIdentifier{})
        : _destination(destination) {}

    const DeviceIdentifier& Destination() const noexcept { return _destination; }
    void SetDestination(const DeviceIdentifier& destination) { _destination = destination; }
    const PendingStateUpdate<Value>& PendingUpdate() const noexcept { return _pending; }

    bool Replace(StateEpoch epoch, StateRevision revision, const Value& value) {
        return _pending.Replace(epoch, revision, value);
    }
    bool Replace(StateEpoch epoch, StateRevision revision, Value&& value) {
        return _pending.Replace(epoch, revision, std::move(value));
    }
    void Clear() noexcept { _pending.Clear(); }
};

} // namespace State
} // namespace ESPressio
