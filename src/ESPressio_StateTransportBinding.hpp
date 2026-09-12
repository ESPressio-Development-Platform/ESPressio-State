#pragma once
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <ESPressio_PrimitivePolicy.hpp>
#include "ESPressio_StateRemoteAdmission.hpp"
#include "ESPressio_StateWireV1.hpp"

namespace ESPressio::State {

enum class StatePayloadFormat : std::uint8_t { DirectBinary=1, CBOR=2, JSON=3 };
constexpr bool IsValidStatePayloadFormat(StatePayloadFormat format) noexcept {
    return format==StatePayloadFormat::DirectBinary || format==StatePayloadFormat::CBOR || format==StatePayloadFormat::JSON;
}

enum class StateTransportAdmissionStatus : std::uint8_t { Accepted, CapacityUnavailable, InvalidDestination, Quiesced };
struct StateTransportAdmission final {
    StateTransportAdmissionStatus Status=StateTransportAdmissionStatus::CapacityUnavailable;
    constexpr explicit operator bool() const noexcept { return Status==StateTransportAdmissionStatus::Accepted; }
};

enum class StateOwnerDiscoveryStatus : std::uint8_t { Success, Unsupported, CapacityUnavailable, Quiesced };

struct StateTransportContract final {
    StateTypeId TypeId{};
    StatePayloadFormat Format=StatePayloadFormat::DirectBinary;
    std::size_t MaximumPublicationWireBytes=0;
    std::size_t MaximumControlWireBytes=StateControlWireHeaderSize;
    std::size_t MaximumSnapshotControlWireBytes=0;
    std::size_t MaximumAcceptanceControlWireBytes=StateAcceptanceControlWireHeaderSize;
    const Primitive::PrimitivePolicyDescriptor* ConvergencePolicy=nullptr;
    bool SupportsOwnerDiscovery=false;
};

/// <summary>One semantic State-family message offered synchronously to an adapter binding.</summary>
/// <remarks>The adapter must retain/copy every field it needs before returning Accepted. The message
/// contains no physical route, link peer, packet identifier, retry object or adapter-owned byte storage.
/// Admit is a bounded nonblocking ownership attempt: it must not wait for capacity or invoke
/// application callbacks. Retry scheduling belongs to the adapter service context.</remarks>
template<class TState>
struct StateOutboundMessage final {
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

/// <summary>Allocation-free sink used by an AnyDevice-capable adapter to enumerate concrete owners.</summary>
struct StateOwnerDiscoverySink final {
    void* Context=nullptr;
    bool (*Offer)(void*,const System::DeviceIdentifier&) noexcept=nullptr;
    constexpr explicit operator bool() const noexcept { return Context && Offer; }
    bool TryOffer(const System::DeviceIdentifier& owner) const noexcept { return *this && owner && Offer(Context,owner); }
};

namespace Detail {
template<class Format> constexpr StatePayloadFormat StateBindingPayloadFormat() noexcept {
    if constexpr(std::is_same_v<Format,Serializable::DirectBinary>) return StatePayloadFormat::DirectBinary;
    else if constexpr(std::is_same_v<Format,Serializable::CBOR>) return StatePayloadFormat::CBOR;
    else {
        static_assert(std::is_same_v<Format,Serializable::JSON>,"Unsupported State transport P3 format");
        return StatePayloadFormat::JSON;
    }
}

template<class TState,class Format>
StateTransportContract MakeStateTransportContract(bool discovery) noexcept {
    static_assert(TState::IsTransmissibleState && TState::ValidateTier());
    static const auto policy=Primitive::PrimitivePolicyContract<typename TState::ConvergencePolicy>::Descriptor();
    return {TState::TypeId,StateBindingPayloadFormat<Format>(),
            MaximumCompleteStatePublicationWireBytes<TState,Format>,StateControlWireHeaderSize,
            MaximumCompleteStateSnapshotControlWireBytes<TState,Format>,StateAcceptanceControlWireHeaderSize,
            &policy,discovery};
}

template<class TState>
struct StateTransportBindingView final {
    void* Owner=nullptr;
    StateTransportContract Contract{};
    bool (*Validate)(void*,const StateTransportContract&) noexcept=nullptr;
    StateTransportAdmission (*Admit)(void*,const StateOutboundMessage<TState>&) noexcept=nullptr;
    StateOwnerDiscoveryStatus (*DiscoverOwners)(void*,StateOwnerDiscoverySink) noexcept=nullptr;
    constexpr explicit operator bool() const noexcept {
        return Owner && Validate && Admit && bool(Contract.TypeId) && IsValidStatePayloadFormat(Contract.Format);
    }
};
}

/// <summary>Frozen typed State-family adapter seam for one TransmissibleState Type and P3 format.</summary>
template<class TState,class Format>
class StateTransportBinding final {
    static_assert(TState::IsTransmissibleState && TState::ValidateTier(),"State transport binding requires TransmissibleState");
    Detail::StateTransportBindingView<TState> _view{};
public:
    StateTransportBinding() noexcept=default;
    StateTransportBinding(const StateTransportBinding&)=delete;
    StateTransportBinding& operator=(const StateTransportBinding&)=delete;

    template<class Owner,
             StateTransportAdmission (Owner::*Admit)(const StateOutboundMessage<TState>&) noexcept,
             bool (Owner::*Validate)(const StateTransportContract&) noexcept>
    bool Initialize(Owner& owner) noexcept {
        if(_view) return false;
        _view.Owner=&owner;
        _view.Contract=Detail::MakeStateTransportContract<TState,Format>(false);
        _view.Validate=[](void* p,const StateTransportContract& contract) noexcept { return (static_cast<Owner*>(p)->*Validate)(contract); };
        _view.Admit=[](void* p,const StateOutboundMessage<TState>& message) noexcept { return (static_cast<Owner*>(p)->*Admit)(message); };
        return true;
    }

    template<class Owner,
             StateTransportAdmission (Owner::*Admit)(const StateOutboundMessage<TState>&) noexcept,
             bool (Owner::*Validate)(const StateTransportContract&) noexcept,
             StateOwnerDiscoveryStatus (Owner::*DiscoverOwners)(StateOwnerDiscoverySink) noexcept>
    bool InitializeWithDiscovery(Owner& owner) noexcept {
        if(_view) return false;
        _view.Owner=&owner;
        _view.Contract=Detail::MakeStateTransportContract<TState,Format>(true);
        _view.Validate=[](void* p,const StateTransportContract& contract) noexcept { return (static_cast<Owner*>(p)->*Validate)(contract); };
        _view.Admit=[](void* p,const StateOutboundMessage<TState>& message) noexcept { return (static_cast<Owner*>(p)->*Admit)(message); };
        _view.DiscoverOwners=[](void* p,StateOwnerDiscoverySink sink) noexcept { return (static_cast<Owner*>(p)->*DiscoverOwners)(sink); };
        return true;
    }

    const Detail::StateTransportBindingView<TState>& View() const noexcept { return _view; }
    const StateTransportContract& Contract() const noexcept { return _view.Contract; }
};

} // namespace ESPressio::State
