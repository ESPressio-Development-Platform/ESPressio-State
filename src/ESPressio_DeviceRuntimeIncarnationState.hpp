#pragma once
#include <cstdint>
#include <string_view>
#include <ESPressio_PrimitivePolicy.hpp>
#include <ESPressio_SerializationMacros.hpp>
#include "ESPressio_TransmissibleState.hpp"

namespace ESPressio::State {

struct DeviceRuntimeIncarnationValue final {
    std::uint32_t Incarnation=0;
    constexpr bool operator==(const DeviceRuntimeIncarnationValue& other) const noexcept {
        return Incarnation==other.Incarnation;
    }
    ESPRESSIO_SERIALIZABLE_TYPE(DeviceRuntimeIncarnationValue)
    ESPRESSIO_SERIALIZABLE_SCHEMA_VERSION(1)
    ESPRESSIO_SERIALIZABLE_PROPERTIES(ESPRESSIO_PROPERTY("incarnation",Incarnation))
};

struct DeviceRuntimeIncarnationConvergence final {
    using PolicyCategory=Primitive::StateConvergencePolicyTag;
    using RequiredEvidence=Primitive::NoRemoteEvidence;
    using Supersession=Primitive::LatestAuthoritativeValue;
    using ExhaustionDisposition=Primitive::DormantNeedsConvergence;
    static constexpr std::uint64_t MaximumResidenceNanoseconds=0;
    static constexpr std::uint16_t MaximumAttempts=1;
    static constexpr std::uint64_t MaximumAdapterAdmissionWaitNanoseconds=0;
    static constexpr std::uint64_t MinimumRetrySpacingNanoseconds=0;
    static constexpr std::uint64_t MaximumRetrySpacingNanoseconds=0;
};

/// <summary>Read-only State projection of System's installed RuntimeIncarnationId.</summary>
/// <remarks>The stable platform-reserved TypeId spells "ESPSTA" plus allocation 1. System remains
/// authoritative; State installs the value once during initialization and exposes no StateOwner.</remarks>
struct DeviceRuntimeIncarnationState final
    : TransmissibleState<DeviceRuntimeIncarnationState,DeviceRuntimeIncarnationValue> {
    static constexpr StateTypeId TypeId{0x4553505354410001ULL};
    static constexpr std::string_view CanonicalName="ESPressio.State.DeviceRuntimeIncarnation";
    using ConvergencePolicy=DeviceRuntimeIncarnationConvergence;
    static constexpr bool IsRuntimeIdentityProjection=true;
};

} // namespace ESPressio::State
