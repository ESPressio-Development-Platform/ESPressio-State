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

template<typename TDefinition>
struct SerializedRemoteState final {
    static constexpr std::size_t MaximumPayloadSize = StateCodec<TDefinition>::MaximumEncodedSize;
    DeviceIdentifier Device{};
    StateTypeId TypeId = StateTypeIdOf<TDefinition>;
    const char* Name = StateNameOf<TDefinition>;
    StateEpoch Epoch = 0;
    StateRevision Revision = 0;
    StateAvailabilityStatus Availability{};
    StateSourceReachability Reachability = StateSourceReachability::Unknown;
    std::array<uint8_t, MaximumPayloadSize> Payload{};
    std::size_t PayloadSize = 0;
};

template<typename TContract>
class StateSerialization final {
private:
    template<typename TDefinition>
    static bool EncodeSnapshot(const RemoteStateIntrospectionSnapshot<TDefinition>& snapshot, SerializedRemoteState<TDefinition>& output) {
        if (!snapshot.State.HasValue) return false;
        output.Device = snapshot.Device;
        output.TypeId = snapshot.TypeId;
        output.Name = snapshot.Name;
        output.Epoch = snapshot.State.Epoch;
        output.Revision = snapshot.State.Revision;
        output.Availability = snapshot.State.Availability;
        output.Reachability = snapshot.State.Reachability;
        output.PayloadSize = 0;
        return StateCodec<TDefinition>::Encode(snapshot.State.Value, output.Payload.data(), output.Payload.size(), output.PayloadSize);
    }

public:
    template<typename TDefinition, std::size_t TMaximumDevices>
    static bool Serialize(const RemoteStateManager<TContract, TMaximumDevices>& manager, const DeviceIdentifier& device,
                          SerializedRemoteState<TDefinition>& output) {
        static_assert(TContract::template Contains<TDefinition>, "State definition is not part of this StateContract");
        RemoteStateIntrospectionSnapshot<TDefinition> snapshot;
        if (!StateIntrospection<TContract>::template Read<TDefinition>(manager, device, snapshot)) return false;
        return EncodeSnapshot(snapshot, output);
    }

    template<std::size_t TMaximumDevices, typename TCallback>
    static bool Serialize(const RemoteStateManager<TContract, TMaximumDevices>& manager, const DeviceIdentifier& device,
                          StateTypeId typeId, TCallback&& callback) {
        bool serialized = false;
        auto visitor = std::forward<TCallback>(callback);
        const bool known = StateIntrospection<TContract>::Visit(typeId, [&](auto tag) {
            using Definition = typename decltype(tag)::Definition;
            SerializedRemoteState<Definition> record;
            if (Serialize<Definition>(manager, device, record)) { visitor(record); serialized = true; }
        });
        return known && serialized;
    }

    template<std::size_t TMaximumDevices, typename TCallback>
    static void ForEachState(const RemoteStateManager<TContract, TMaximumDevices>& manager, const DeviceIdentifier& device, TCallback&& callback) {
        auto visitor = std::forward<TCallback>(callback);
        StateIntrospection<TContract>::ForEachState(manager, device, [&](const auto& snapshot) {
            using Snapshot = std::decay_t<decltype(snapshot)>;
            using Definition = typename Snapshot::Definition;
            SerializedRemoteState<Definition> record;
            if (EncodeSnapshot(snapshot, record)) visitor(record);
        });
    }

    template<std::size_t TMaximumDevices, typename TCallback>
    static void ForEachRemoteState(const RemoteStateManager<TContract, TMaximumDevices>& manager, TCallback&& callback) {
        auto visitor = std::forward<TCallback>(callback);
        StateIntrospection<TContract>::ForEachRemoteState(manager, [&](const auto& snapshot) {
            using Snapshot = std::decay_t<decltype(snapshot)>;
            using Definition = typename Snapshot::Definition;
            SerializedRemoteState<Definition> record;
            if (EncodeSnapshot(snapshot, record)) visitor(record);
        });
    }
};

} // namespace State
} // namespace ESPressio

#endif // ESPRESSIO_STATE_ENABLE_SERIALIZATION
