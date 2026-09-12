#pragma once
#include <cstddef>
#include <cstdint>
#include <ESPressio_PrimitiveAdmission.hpp>
#include "ESPressio_StateWireV1.hpp"

namespace ESPressio::State {

/// <summary>Adapter-authenticated original semantic source for one inbound State message.</summary>
/// <remarks>The immediate radio/mesh/socket peer is not sufficient. A production adapter may construct
/// this context only after validating the original semantic source across any relay path.</remarks>
struct StateValidatedIngressContext final {
    System::DeviceRuntimeIdentity OriginalSource{};
    constexpr explicit operator bool() const noexcept { return bool(OriginalSource); }
};

enum class StateSemanticSourceRole : std::uint8_t { Owner, Requester };

constexpr StateSemanticSourceRole StateMessageSourceRole(StateMessageKind kind) noexcept {
    switch(kind) {
        case StateMessageKind::Publication:
        case StateMessageKind::SubscribeSnapshot:
        case StateMessageKind::SubscribeNoValue:
        case StateMessageKind::SubscribeRejected:
        case StateMessageKind::BaselineSnapshot:
        case StateMessageKind::ResyncSnapshot:
        case StateMessageKind::ResyncRequired:
            return StateSemanticSourceRole::Owner;
        case StateMessageKind::PublicationAccepted:
        case StateMessageKind::SubscribeRequest:
        case StateMessageKind::SubscribeAccepted:
        case StateMessageKind::UnsubscribeRequest:
        case StateMessageKind::BaselineAccepted:
        case StateMessageKind::ResyncRequest:
        case StateMessageKind::ResyncAccepted:
            return StateSemanticSourceRole::Requester;
    }
    return StateSemanticSourceRole::Owner;
}

constexpr bool ValidateStateSemanticSource(StateMessageKind kind,
                                           const System::DeviceRuntimeIdentity& owner,
                                           const System::DeviceRuntimeIdentity& requester,
                                           StateValidatedIngressContext ingress) noexcept {
    if(!ingress || !IsValidStateMessageKind(kind)) return false;
    return StateMessageSourceRole(kind)==StateSemanticSourceRole::Owner
        ? owner==ingress.OriginalSource
        : requester==ingress.OriginalSource;
}

template<class TState>
struct StateDecodedIngress final {
    StateMessageKind Kind=StateMessageKind::Publication;
    System::DeviceRuntimeIdentity Owner{};
    System::DeviceRuntimeIdentity Requester{};
    StateSessionToken Session{};
    StateResyncToken Resync{};
    StateVersion Version{};
    StateSnapshot<TState> Snapshot{};
    bool HasSnapshot=false;
    std::uint8_t ControlCode=0;
};

struct StateRemoteAdmissionResult final {
    Primitive::PrimitiveAdmissionDisposition Disposition=Primitive::PrimitiveAdmissionDisposition::Malformed;
    StateWireStatus WireStatus=StateWireStatus::InvalidHeader;
    constexpr explicit operator bool() const noexcept {
        return Primitive::EstablishesDestinationAdmission(Disposition);
    }
};

namespace Detail {
inline StateRemoteAdmissionResult StateWireFailure(StateWireStatus status) noexcept {
    using D=Primitive::PrimitiveAdmissionDisposition;
    return {status==StateWireStatus::UnsupportedProtocol ? D::Unsupported : D::Malformed,status};
}
inline StateRemoteAdmissionResult StateProvenanceFailure() noexcept {
    return {Primitive::PrimitiveAdmissionDisposition::Rejected,StateWireStatus::Success};
}
}

/// <summary>Boundedly decodes one Type-specific State V1 message after validating semantic provenance.</summary>
/// <remarks>No State runtime/session mutation occurs here. The returned typed value is independent of the
/// borrowed ingress bytes. This function is the family security boundary used by adapter-facing ingress:
/// malformed bytes or semantic-source mismatch cannot reach State mutation APIs.</remarks>
template<class TState,class Format>
StateRemoteAdmissionResult DecodeValidatedStateIngress(const std::uint8_t* data,std::size_t size,
                                                       StateValidatedIngressContext ingress,
                                                       StateDecodedIngress<TState>& output) {
    static_assert(TState::IsTransmissibleState && TState::ValidateTier(),
                  "Remote State admission requires a valid TransmissibleState Type");
    using D=Primitive::PrimitiveAdmissionDisposition;
    if(!data || size<5) return {};
    const auto rawKind=static_cast<StateMessageKind>(data[4]);
    if(!IsValidStateMessageKind(rawKind)) return {};

    StateDecodedIngress<TState> decoded{};
    decoded.Kind=rawKind;

    if(rawKind==StateMessageKind::Publication) {
        StatePublicationWireHeader header{};
        const auto h=DecodeStatePublicationHeader(data,size,header);
        if(!h) return Detail::StateWireFailure(h.Status);
        if(header.TypeId!=TState::TypeId) return {D::Unsupported,StateWireStatus::Success};
        if(!ValidateStateSemanticSource(rawKind,header.Owner,header.Requester,ingress))
            return Detail::StateProvenanceFailure();
        typename TState::ValueType value{};
        const auto payload=DecodeStatePublicationValue<TState,Format>(header,data+StatePublicationWireHeaderSize,
                                                                      size-StatePublicationWireHeaderSize,value);
        if(!payload) return Detail::StateWireFailure(payload.Status);
        decoded.Owner=header.Owner;decoded.Requester=header.Requester;decoded.Session=header.Session;
        decoded.Version=header.Version;decoded.Snapshot={value,header.TruthTime};decoded.HasSnapshot=true;
    } else if(IsStateSnapshotControlKind(rawKind)) {
        StateSnapshotControlWireHeader header{};
        const auto h=DecodeStateSnapshotControlHeader(data,size,header);
        if(!h) return Detail::StateWireFailure(h.Status);
        if(header.Control.TypeId!=TState::TypeId) return {D::Unsupported,StateWireStatus::Success};
        if(!ValidateStateSemanticSource(rawKind,header.Control.Owner,header.Control.Requester,ingress))
            return Detail::StateProvenanceFailure();
        typename TState::ValueType value{};
        const auto payload=DecodeStateSnapshotControlValue<TState,Format>(header,data+StateSnapshotControlWireHeaderSize,
                                                                          size-StateSnapshotControlWireHeaderSize,value);
        if(!payload) return Detail::StateWireFailure(payload.Status);
        decoded.Owner=header.Control.Owner;decoded.Requester=header.Control.Requester;decoded.Session=header.Control.Session;
        decoded.Resync=header.Control.Resync;decoded.Version=header.Version;
        decoded.Snapshot={value,header.TruthTime};decoded.HasSnapshot=true;decoded.ControlCode=header.Control.ControlCode;
    } else if(IsStateAcceptanceControlKind(rawKind)) {
        StateAcceptanceControlWireHeader header{};
        const auto h=DecodeStateAcceptanceControl(data,size,header);
        if(!h) return Detail::StateWireFailure(h.Status);
        if(header.Control.TypeId!=TState::TypeId) return {D::Unsupported,StateWireStatus::Success};
        if(!ValidateStateSemanticSource(rawKind,header.Control.Owner,header.Control.Requester,ingress))
            return Detail::StateProvenanceFailure();
        decoded.Owner=header.Control.Owner;decoded.Requester=header.Control.Requester;decoded.Session=header.Control.Session;
        decoded.Resync=header.Control.Resync;decoded.Version=header.Version;decoded.ControlCode=header.Control.ControlCode;
    } else {
        StateControlWireHeader header{};
        const auto h=DecodeStateControl(data,size,header);
        if(!h) return Detail::StateWireFailure(h.Status);
        if(header.TypeId!=TState::TypeId) return {D::Unsupported,StateWireStatus::Success};
        if(!ValidateStateSemanticSource(rawKind,header.Owner,header.Requester,ingress))
            return Detail::StateProvenanceFailure();
        decoded.Owner=header.Owner;decoded.Requester=header.Requester;decoded.Session=header.Session;
        decoded.Resync=header.Resync;decoded.ControlCode=header.ControlCode;
    }

    output=decoded;
    return {D::Accepted,StateWireStatus::Success};
}

} // namespace ESPressio::State
