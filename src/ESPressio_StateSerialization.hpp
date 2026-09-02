#pragma once

#ifndef ESPRESSIO_STATE_ENABLE_SERIALIZATION
#define ESPRESSIO_STATE_ENABLE_SERIALIZATION 1
#endif

#if ESPRESSIO_STATE_ENABLE_SERIALIZATION

#ifndef ESPRESSIO_STATE_ENABLE_INTROSPECTION
#define ESPRESSIO_STATE_ENABLE_INTROSPECTION 1
#endif

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

#include "ESPressio_StateCodec.hpp"
#include "ESPressio_StateIntrospection.hpp"

namespace ESPressio {
namespace State {

/// <summary>Serialized representation of one typed remote-state snapshot.</summary>
/// <typeparam name="TDefinition">State definition whose value is represented.</typeparam>
template<typename TDefinition>
struct SerializedRemoteState final {
    /// <summary>Maximum encoded payload size for the state definition.</summary>
    static constexpr std::size_t MaximumPayloadSize =
        StateCodec<TDefinition>::MaximumEncodedSize;

    /// <summary>Device that owns the remote state.</summary>
    DeviceIdentifier Device{};
    /// <summary>Stable identifier of the serialized state type.</summary>
    StateTypeId TypeId = StateTypeIdOf<TDefinition>;
    /// <summary>Human-readable state name supplied by state introspection.</summary>
    const char* Name = StateNameOf<TDefinition>;
    /// <summary>Remote state epoch associated with the serialized value.</summary>
    StateEpoch Epoch = 0;
    /// <summary>Remote state revision associated with the serialized value.</summary>
    StateRevision Revision = 0;
    /// <summary>Known availability of the remote device when the snapshot was captured.</summary>
    RemoteDeviceAvailability Availability = RemoteDeviceAvailability::Unknown;
    /// <summary>Encoded state payload storage.</summary>
    std::array<uint8_t, MaximumPayloadSize> Payload{};
    /// <summary>Number of valid bytes currently stored in <c>Payload</c>.</summary>
    std::size_t PayloadSize = 0;
};

/// <summary>Serializes typed remote-state snapshots exposed by a state contract.</summary>
/// <typeparam name="TContract">State contract describing the supported state definitions.</typeparam>
template<typename TContract>
class StateSerialization final {
private:
    template<typename TDefinition>
    static bool EncodeSnapshot(
        const RemoteStateIntrospectionSnapshot<TDefinition>& snapshot,
        SerializedRemoteState<TDefinition>& output
    ) {
        if (!snapshot.State.HasValue) return false;

        output.Device = snapshot.Device;
        output.TypeId = snapshot.TypeId;
        output.Name = snapshot.Name;
        output.Epoch = snapshot.State.Epoch;
        output.Revision = snapshot.State.Revision;
        output.Availability = snapshot.State.Availability;
        output.PayloadSize = 0;

        return StateCodec<TDefinition>::Encode(
            snapshot.State.Value,
            output.Payload.data(),
            output.Payload.size(),
            output.PayloadSize
        );
    }

public:
    /// <summary>Serializes one typed state value for a remote device.</summary>
    /// <typeparam name="TDefinition">State definition to serialize.</typeparam>
    /// <typeparam name="TMaximumDevices">Maximum remote-device capacity of the manager.</typeparam>
    /// <param name="manager">Remote state manager containing the state snapshot.</param>
    /// <param name="device">Remote device whose state is required.</param>
    /// <param name="output">Receives the serialized state record.</param>
    /// <returns><c>true</c> when a current value exists and is successfully encoded.</returns>
    template<typename TDefinition, std::size_t TMaximumDevices>
    static bool Serialize(
        const RemoteStateManager<TContract, TMaximumDevices>& manager,
        const DeviceIdentifier& device,
        SerializedRemoteState<TDefinition>& output
    ) {
        static_assert(
            TContract::template Contains<TDefinition>,
            "State definition is not part of this StateContract"
        );

        RemoteStateIntrospectionSnapshot<TDefinition> snapshot;
        if (!StateIntrospection<TContract>::template Read<TDefinition>(
                manager,
                device,
                snapshot
            )) {
            return false;
        }
        return EncodeSnapshot(snapshot, output);
    }

    /// <summary>Serializes a remote state selected at runtime by its stable type identifier.</summary>
    /// <typeparam name="TMaximumDevices">Maximum remote-device capacity of the manager.</typeparam>
    /// <typeparam name="TCallback">Callback receiving the strongly typed serialized record.</typeparam>
    /// <param name="manager">Remote state manager containing the state snapshot.</param>
    /// <param name="device">Remote device whose state is required.</param>
    /// <param name="typeId">Stable state type identifier to serialize.</param>
    /// <param name="callback">Callback invoked with the serialized record.</param>
    /// <returns><c>true</c> when the type is known and a value was serialized.</returns>
    template<std::size_t TMaximumDevices, typename TCallback>
    static bool Serialize(
        const RemoteStateManager<TContract, TMaximumDevices>& manager,
        const DeviceIdentifier& device,
        StateTypeId typeId,
        TCallback&& callback
    ) {
        bool serialized = false;
        auto visitor = std::forward<TCallback>(callback);
        const bool known = StateIntrospection<TContract>::Visit(
            typeId,
            [&](auto tag) {
                using Definition = typename decltype(tag)::Definition;
                SerializedRemoteState<Definition> record;
                if (Serialize<Definition>(manager, device, record)) {
                    visitor(record);
                    serialized = true;
                }
            }
        );
        return known && serialized;
    }

    /// <summary>Serializes each currently available state value for one remote device.</summary>
    /// <typeparam name="TMaximumDevices">Maximum remote-device capacity of the manager.</typeparam>
    /// <typeparam name="TCallback">Callback receiving each strongly typed serialized record.</typeparam>
    template<std::size_t TMaximumDevices, typename TCallback>
    static void ForEachState(
        const RemoteStateManager<TContract, TMaximumDevices>& manager,
        const DeviceIdentifier& device,
        TCallback&& callback
    ) {
        auto visitor = std::forward<TCallback>(callback);
        StateIntrospection<TContract>::ForEachState(
            manager,
            device,
            [&](const auto& snapshot) {
                using Snapshot = std::decay_t<decltype(snapshot)>;
                using Definition = typename Snapshot::Definition;
                SerializedRemoteState<Definition> record;
                if (EncodeSnapshot(snapshot, record)) visitor(record);
            }
        );
    }

    /// <summary>Serializes each currently available state value across all known remote devices.</summary>
    /// <typeparam name="TMaximumDevices">Maximum remote-device capacity of the manager.</typeparam>
    /// <typeparam name="TCallback">Callback receiving each strongly typed serialized record.</typeparam>
    template<std::size_t TMaximumDevices, typename TCallback>
    static void ForEachRemoteState(
        const RemoteStateManager<TContract, TMaximumDevices>& manager,
        TCallback&& callback
    ) {
        auto visitor = std::forward<TCallback>(callback);
        StateIntrospection<TContract>::ForEachRemoteState(
            manager,
            [&](const auto& snapshot) {
                using Snapshot = std::decay_t<decltype(snapshot)>;
                using Definition = typename Snapshot::Definition;
                SerializedRemoteState<Definition> record;
                if (EncodeSnapshot(snapshot, record)) visitor(record);
            }
        );
    }
};

}
}

#endif // ESPRESSIO_STATE_ENABLE_SERIALIZATION
