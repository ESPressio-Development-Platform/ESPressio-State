#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <type_traits>
#include <ESPressio_DeviceRuntimeIdentity.hpp>
#include <ESPressio_PrimitivePolicy.hpp>
#include <ESPressio_Synchronization.hpp>
#include "ESPressio_StateRemoteSession.hpp"
#include "ESPressio_StateSnapshot.hpp"
#include "ESPressio_StateVersion.hpp"
#include "ESPressio_StateWireV1.hpp"

namespace ESPressio::State {

template<class TState>
struct StateReplicaComparison {
    using Value=typename TState::ValueType;
    static constexpr bool EqualsExact(const Value& left,const Value& right) noexcept(noexcept(left==right)) {
        static_assert(noexcept(left==right),"Remote same-version State comparison must be noexcept or provide StateReplicaComparison specialization");
        return left==right;
    }
};

template<class TState>
struct StateRemoteOwnerSlot final {
    bool Occupied=false;
    System::DeviceRuntimeIdentity Owner{};
    StateSessionToken Session{};
    StateRemoteSessionState SessionState=StateRemoteSessionState::Inactive;
    StateVersion Baseline{};
    StateSnapshot<TState> Snapshot{};
    bool HasValue=false;
    StateResyncToken Resync{};
    StateResyncToken LastAcceptedResync{};
    StateVersion LastAcceptedResyncVersion{};
};

template<class TState>
struct StateSourceSubscriberSlot final {
    bool Occupied=false;
    System::DeviceRuntimeIdentity Requester{};
    StateSessionToken Session{};
    StateRemoteSessionState SessionState=StateRemoteSessionState::Inactive;
    StateVersion AcceptedBaseline{};
    bool HasAcceptedBaseline=false;
    bool Dirty=false;
    StateResyncToken Resync{};
    StateVersion OfferedBaseline{};
    bool HasOfferedBaseline=false;
    StateVersion LatestVersion{};
    // Establishment acceptance carries no compact version. Its baseline must
    // therefore remain immutable across retries until that acceptance arrives.
    bool HasEstablishmentReply=false;
    bool HasPendingFirstBaseline=false;
    bool ResyncRequiredPending=false;
    StateSnapshot<TState> ControlSnapshot{};
    StateResyncToken ResyncHighWater{};
};

struct StateSourceWork final {
    System::DeviceRuntimeIdentity Requester{};
    StateSessionToken Session{};
    StateMessageKind Kind=StateMessageKind::Publication;
    StateVersion Version{};
};

template<class TState>
struct StateConvergenceBindingView final {
    void* Owner=nullptr;
    void (*MarkLatestDirty)(void*,StateVersion) noexcept=nullptr;
    constexpr explicit operator bool() const noexcept { return Owner && MarkLatestDirty; }
};

/// <summary>Fixed per-Type remote-owner and source-subscriber State convergence storage.</summary>
/// <remarks>No slot stores transport reachability/freshness or an unbounded revision history. Full capacity
/// rejects a new semantic owner/requester; existing slots are never silently evicted. Last-known remote
/// snapshots remain readable independently of session state.</remarks>
template<class TState,std::size_t RemoteOwners,std::size_t Subscribers>
class StateRemoteReplicaTable final {
    using Value=typename TState::ValueType;
    mutable System::Synchronization::Mutex _mutex;
    std::array<StateRemoteOwnerSlot<TState>,RemoteOwners> _owners{};
    std::array<StateSourceSubscriberSlot<TState>,Subscribers> _subscribers{};

    static bool SnapshotExact(const StateSnapshot<TState>& a,const StateSnapshot<TState>& b) noexcept {
        return StateReplicaComparison<TState>::EqualsExact(a.Value,b.Value) &&
               a.TruthTime.Nanoseconds==b.TruthTime.Nanoseconds &&
               a.TruthTime.Reliability==b.TruthTime.Reliability;
    }
    StateRemoteOwnerSlot<TState>* FindOwnerLocked(const System::DeviceIdentifier& device) noexcept {
        for(auto& slot:_owners) {
            if(slot.Occupied && slot.Owner.Device==device) return &slot;
        }
        return nullptr;
    }
    const StateRemoteOwnerSlot<TState>* FindOwnerLocked(const System::DeviceIdentifier& device) const noexcept {
        for(const auto& slot:_owners) {
            if(slot.Occupied && slot.Owner.Device==device) return &slot;
        }
        return nullptr;
    }
    StateSourceSubscriberSlot<TState>* FindSubscriberLocked(const System::DeviceIdentifier& device) noexcept {
        for(auto& slot:_subscribers) {
            if(slot.Occupied && slot.Requester.Device==device) return &slot;
        }
        return nullptr;
    }
    const StateSourceSubscriberSlot<TState>* FindSubscriberLocked(const System::DeviceIdentifier& device) const noexcept {
        for(const auto& slot:_subscribers) {
            if(slot.Occupied && slot.Requester.Device==device) return &slot;
        }
        return nullptr;
    }
public:
    static constexpr std::size_t RemoteOwnerCapacity=RemoteOwners;
    static constexpr std::size_t SubscriberCapacity=Subscribers;
    static constexpr bool RequiresAcknowledgement=[] {
        if constexpr(TState::IsTransmissibleState)
            return std::is_same_v<typename TState::ConvergencePolicy::RequiredEvidence,Primitive::DestinationPrimitiveAdmission>;
        else return false;
    }();

    StateRemoteStatus ReserveRemoteOwner(const System::DeviceIdentifier& device,StateSessionToken session) noexcept {
        if(!device || !session) return StateRemoteStatus::InvalidSession;
        std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
        auto* slot=FindOwnerLocked(device);
        if(!slot) {
            for(auto& candidate:_owners) {
                if(!candidate.Occupied) { slot=&candidate; break; }
            }
        }
        if(!slot) return StateRemoteStatus::CapacityUnavailable;
        slot->Occupied=true;
        slot->Owner={device,{}};
        slot->Session=session;
        slot->SessionState=StateRemoteSessionState::Establishing;
        slot->Baseline={};
        slot->Resync={};
        return StateRemoteStatus::Success;
    }

    StateRemoteStatus InstallSubscribeSnapshot(const System::DeviceRuntimeIdentity& owner,StateSessionToken session,
                                               StateVersion version,const StateSnapshot<TState>& snapshot) noexcept {
        if(!owner || !session || !version) return StateRemoteStatus::InvalidIdentity;
        std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
        auto* slot=FindOwnerLocked(owner.Device);
        if(!slot) return StateRemoteStatus::NotFound;
        if(slot->Session!=session) return StateRemoteStatus::SessionMismatch;
        if(slot->SessionState==StateRemoteSessionState::ActiveTrusted && slot->Owner==owner &&
           slot->Baseline==version && slot->HasValue && SnapshotExact(slot->Snapshot,snapshot))
            return StateRemoteStatus::Duplicate;
        if(slot->SessionState!=StateRemoteSessionState::Establishing) return StateRemoteStatus::SessionMismatch;
        slot->Owner=owner;
        slot->Baseline=version;
        slot->Snapshot=snapshot;
        slot->HasValue=true;
        slot->SessionState=StateRemoteSessionState::ActiveTrusted;
        slot->Resync={};
        return StateRemoteStatus::Success;
    }
    StateRemoteStatus InstallSubscribeNoValue(const System::DeviceRuntimeIdentity& owner,StateSessionToken session) noexcept {
        if(!owner || !session) return StateRemoteStatus::InvalidIdentity;
        std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
        auto* slot=FindOwnerLocked(owner.Device);
        if(!slot) return StateRemoteStatus::NotFound;
        if(slot->Session!=session) return StateRemoteStatus::SessionMismatch;
        if(slot->SessionState==StateRemoteSessionState::ActiveNoBaseline && slot->Owner==owner)
            return StateRemoteStatus::Duplicate;
        if(slot->SessionState!=StateRemoteSessionState::Establishing) return StateRemoteStatus::SessionMismatch;
        slot->Owner=owner;
        slot->Baseline={};
        slot->SessionState=StateRemoteSessionState::ActiveNoBaseline;
        slot->Resync={};
        return StateRemoteStatus::Success;
    }
    StateRemoteStatus InstallBaselineSnapshot(const System::DeviceRuntimeIdentity& owner,StateSessionToken session,
                                              StateVersion version,const StateSnapshot<TState>& snapshot) noexcept {
        if(!owner || !session || !version) return StateRemoteStatus::InvalidIdentity;
        std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
        auto* slot=FindOwnerLocked(owner.Device);
        if(!slot) return StateRemoteStatus::NotFound;
        if(slot->Session!=session || slot->Owner!=owner)
            return StateRemoteStatus::SessionMismatch;
        if(slot->SessionState==StateRemoteSessionState::ActiveTrusted && slot->Baseline==version &&
           slot->HasValue && SnapshotExact(slot->Snapshot,snapshot)) return StateRemoteStatus::Duplicate;
        if(slot->SessionState!=StateRemoteSessionState::ActiveNoBaseline) return StateRemoteStatus::SessionMismatch;
        slot->Snapshot=snapshot;
        slot->HasValue=true;
        slot->Baseline=version;
        slot->SessionState=StateRemoteSessionState::ActiveTrusted;
        return StateRemoteStatus::Success;
    }

    StateRemoteStatus ApplyPublication(const System::DeviceRuntimeIdentity& owner,StateSessionToken session,
                                       StateVersion version,const StateSnapshot<TState>& snapshot) noexcept {
        if(!owner || !session || !version) return StateRemoteStatus::InvalidIdentity;
        std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
        auto* slot=FindOwnerLocked(owner.Device);
        if(!slot) return StateRemoteStatus::NotFound;
        if(slot->Session!=session || slot->Owner!=owner) return StateRemoteStatus::SessionMismatch;
        if(slot->SessionState==StateRemoteSessionState::AwaitingResync) return StateRemoteStatus::AwaitingResync;
        if(slot->SessionState!=StateRemoteSessionState::ActiveTrusted) {
            slot->SessionState=StateRemoteSessionState::ResyncRequired;
            return StateRemoteStatus::ResyncRequired;
        }
        switch(CompareStateVersion(slot->Baseline,version)) {
            case StateVersionRelation::Duplicate:
                if(slot->HasValue && SnapshotExact(slot->Snapshot,snapshot)) return StateRemoteStatus::Duplicate;
                slot->SessionState=StateRemoteSessionState::ResyncRequired;
                return StateRemoteStatus::ResyncRequired;
            case StateVersionRelation::Older:
                return StateRemoteStatus::Older;
            case StateVersionRelation::Ambiguous:
                slot->SessionState=StateRemoteSessionState::ResyncRequired;
                return StateRemoteStatus::ResyncRequired;
            case StateVersionRelation::Newer:
                slot->Snapshot=snapshot;
                slot->HasValue=true;
                slot->Baseline=version;
                return StateRemoteStatus::Success;
        }
        return StateRemoteStatus::Conflict;
    }

    StateRemoteStatus RequireResync(const System::DeviceIdentifier& device) noexcept {
        std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
        auto* slot=FindOwnerLocked(device);
        if(!slot) return StateRemoteStatus::NotFound;
        slot->SessionState=StateRemoteSessionState::ResyncRequired;
        slot->Resync={};
        return StateRemoteStatus::Success;
    }
    StateRemoteStatus BeginResync(const System::DeviceIdentifier& device,StateResyncToken token) noexcept {
        if(!token) return StateRemoteStatus::InvalidSession;
        std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
        auto* slot=FindOwnerLocked(device);
        if(!slot) return StateRemoteStatus::NotFound;
        if(slot->SessionState!=StateRemoteSessionState::ResyncRequired && slot->SessionState!=StateRemoteSessionState::AwaitingResync)
            return StateRemoteStatus::Conflict;
        slot->Resync=token;
        slot->SessionState=StateRemoteSessionState::AwaitingResync;
        return StateRemoteStatus::Success;
    }
    /// <summary>Starts a wire-requested resync only for the exact live owner session.</summary>
    /// <remarks>Validate identity and allocate the fresh retry token in the same table transaction.
    /// A delayed control must neither invalidate a replacement session nor revive a closed one.
    /// Exhaustion leaves the current baseline and outstanding resync token untouched.</remarks>
    StateRemoteStatus BeginRemoteResync(const System::DeviceRuntimeIdentity& owner,StateSessionToken session,
                                       StateResyncToken& token) noexcept {
        if(!owner || !session) return StateRemoteStatus::InvalidIdentity;
        std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
        auto* slot=FindOwnerLocked(owner.Device);
        if(!slot) return StateRemoteStatus::NotFound;
        if(slot->Owner!=owner || slot->Session!=session) return StateRemoteStatus::SessionMismatch;
        if(slot->SessionState!=StateRemoteSessionState::ActiveTrusted &&
           slot->SessionState!=StateRemoteSessionState::ActiveNoBaseline &&
           slot->SessionState!=StateRemoteSessionState::ResyncRequired &&
           slot->SessionState!=StateRemoteSessionState::AwaitingResync)
            return StateRemoteStatus::SessionMismatch;
        StateResyncToken fresh{};
        if(!Detail::StateProcessTokenAuthority::TryAllocate(fresh)) return StateRemoteStatus::TokenExhausted;
        slot->Resync=fresh;
        slot->SessionState=StateRemoteSessionState::AwaitingResync;
        token=fresh;
        return StateRemoteStatus::Success;
    }
    StateRemoteStatus InstallResyncSnapshot(const System::DeviceRuntimeIdentity& owner,StateSessionToken session,StateResyncToken token,
                                            StateVersion version,const StateSnapshot<TState>& snapshot) noexcept {
        if(!owner || !session || !token || !version) return StateRemoteStatus::InvalidIdentity;
        std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
        auto* slot=FindOwnerLocked(owner.Device);
        if(!slot) return StateRemoteStatus::NotFound;
        if(slot->Owner!=owner || slot->Session!=session)
            return StateRemoteStatus::SessionMismatch;
        if(slot->SessionState==StateRemoteSessionState::ActiveTrusted && slot->LastAcceptedResync==token &&
           slot->LastAcceptedResyncVersion==version && slot->Baseline==version && slot->HasValue &&
           SnapshotExact(slot->Snapshot,snapshot)) return StateRemoteStatus::Duplicate;
        if(slot->SessionState!=StateRemoteSessionState::AwaitingResync || slot->Resync!=token)
            return StateRemoteStatus::SessionMismatch;
        slot->Snapshot=snapshot;
        slot->HasValue=true;
        slot->Baseline=version;
        slot->LastAcceptedResync=token;
        slot->LastAcceptedResyncVersion=version;
        slot->Resync={};
        slot->SessionState=StateRemoteSessionState::ActiveTrusted;
        return StateRemoteStatus::Success;
    }

    bool TryReadRemote(const System::DeviceIdentifier& owner,StateSnapshot<TState>& output) const noexcept {
        std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
        const auto* slot=FindOwnerLocked(owner);
        if(!slot || !slot->HasValue) return false;
        output=slot->Snapshot;
        return true;
    }
    StateRemoteSessionState SessionState(const System::DeviceIdentifier& owner) const noexcept {
        std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
        const auto* slot=FindOwnerLocked(owner);
        return slot?slot->SessionState:StateRemoteSessionState::Inactive;
    }
    StateVersion RemoteBaseline(const System::DeviceIdentifier& owner) const noexcept {
        std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
        const auto* slot=FindOwnerLocked(owner);
        return slot?slot->Baseline:StateVersion{};
    }
    /// <summary>Closes a session and optionally copies its learned owner identity under the same lock.</summary>
    /// <remarks>A supplied token must match before any mutation, preventing an old handle from closing
    /// a replacement session. The identity copy permits notification after ReleaseReplica erases storage.</remarks>
    StateRemoteStatus Unsubscribe(const System::DeviceIdentifier& owner,StateReplicaRelease disposition,
                                  StateSessionToken expected={},System::DeviceRuntimeIdentity* closedOwner=nullptr) noexcept {
        std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
        auto* slot=FindOwnerLocked(owner);
        if(!slot) return StateRemoteStatus::NotFound;
        if(expected && slot->Session!=expected) return StateRemoteStatus::SessionMismatch;
        if(closedOwner) *closedOwner=slot->Owner;
        if(disposition==StateReplicaRelease::ReleaseReplica) {
            *slot={};
            return StateRemoteStatus::Success;
        }
        slot->Session={};
        slot->Resync={};
        slot->Baseline={};
        slot->SessionState=StateRemoteSessionState::Inactive;
        return StateRemoteStatus::Success;
    }
    StateRemoteStatus RejectSubscription(const System::DeviceIdentifier& owner,StateSessionToken expected) noexcept {
        std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
        auto* slot=FindOwnerLocked(owner);
        if(!slot) return StateRemoteStatus::NotFound;
        if(slot->Session!=expected || slot->SessionState!=StateRemoteSessionState::Establishing)
            return StateRemoteStatus::SessionMismatch;
        if(!slot->HasValue) *slot={};
        else {
            slot->Owner.Incarnation={};
            slot->Session={};slot->Resync={};slot->Baseline={};
            slot->SessionState=StateRemoteSessionState::Inactive;
        }
        return StateRemoteStatus::Success;
    }
    /// <summary>Releases only an inactive retained replica; active sessions require explicit Unsubscribe.</summary>
    StateRemoteStatus ForgetRemote(const System::DeviceIdentifier& owner) noexcept {
        std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
        auto* slot=FindOwnerLocked(owner);
        if(!slot) return StateRemoteStatus::NotFound;
        if(slot->SessionState!=StateRemoteSessionState::Inactive || slot->Session) return StateRemoteStatus::Conflict;
        *slot={};
        return StateRemoteStatus::Success;
    }
    std::size_t RemoteOwnersInUse() const noexcept {
        std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
        std::size_t count=0;
        for(const auto& slot:_owners) if(slot.Occupied) ++count;
        return count;
    }

    StateRemoteStatus ReserveSubscriber(const System::DeviceRuntimeIdentity& requester,StateSessionToken session) noexcept {
        if(!requester || !session) return StateRemoteStatus::InvalidIdentity;
        std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
        auto* slot=FindSubscriberLocked(requester.Device);
        if(slot) {
            if(slot->Requester==requester && slot->Session==session) return StateRemoteStatus::Duplicate;
            if(slot->Requester.Incarnation.Value()>requester.Incarnation.Value()) return StateRemoteStatus::SessionMismatch;
            if(slot->Requester==requester && session.Value()<slot->Session.Value()) return StateRemoteStatus::SessionMismatch;
        } else {
            for(auto& candidate:_subscribers) {
                if(!candidate.Occupied) { slot=&candidate; break; }
            }
        }
        if(!slot) return StateRemoteStatus::CapacityUnavailable;
        *slot={};
        slot->Occupied=true;
        slot->Requester=requester;
        slot->Session=session;
        slot->SessionState=StateRemoteSessionState::Establishing;
        return StateRemoteStatus::Success;
    }
    /// <summary>Captures one immutable initial reply for a concrete subscription attempt.</summary>
    /// <remarks>Retries reuse the original fact or NoValue result. SubscribeAccepted has
    /// no version field, so changing that reply would acknowledge an ambiguous baseline.</remarks>
    StateRemoteStatus PrepareSubscriberEstablishment(const System::DeviceRuntimeIdentity& requester,
        StateSessionToken session,bool& hasValue,StateVersion& version,StateSnapshot<TState>& snapshot) noexcept {
        std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
        auto* slot=FindSubscriberLocked(requester.Device);
        if(!slot) return StateRemoteStatus::NotFound;
        if(slot->Requester!=requester || slot->Session!=session) return StateRemoteStatus::SessionMismatch;
        if(slot->SessionState!=StateRemoteSessionState::Establishing) return StateRemoteStatus::Duplicate;
        if(!slot->HasEstablishmentReply) {
            if(hasValue && !version) return StateRemoteStatus::InvalidIdentity;
            slot->HasOfferedBaseline=hasValue;
            slot->OfferedBaseline=hasValue?version:StateVersion{};
            if(hasValue) {
                StateStorageTraits<Value>::CopyOut(snapshot.Value,slot->ControlSnapshot.Value);
                slot->ControlSnapshot.TruthTime=snapshot.TruthTime;
            }
            slot->HasEstablishmentReply=true;
        }
        hasValue=slot->HasOfferedBaseline;
        version=slot->OfferedBaseline;
        if(hasValue) {
            StateStorageTraits<Value>::CopyOut(slot->ControlSnapshot.Value,snapshot.Value);
            snapshot.TruthTime=slot->ControlSnapshot.TruthTime;
        }
        return StateRemoteStatus::Success;
    }
    StateRemoteStatus OfferSubscriberBaseline(const System::DeviceRuntimeIdentity& requester,StateSessionToken session,
                                              bool hasBaseline,StateVersion baseline={}) noexcept {
        std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
        auto* slot=FindSubscriberLocked(requester.Device);
        if(!slot) return StateRemoteStatus::NotFound;
        if(slot->Requester!=requester || slot->Session!=session || slot->SessionState!=StateRemoteSessionState::Establishing)
            return StateRemoteStatus::SessionMismatch;
        slot->HasOfferedBaseline=hasBaseline;
        slot->OfferedBaseline=hasBaseline?baseline:StateVersion{};
        return StateRemoteStatus::Success;
    }
    StateRemoteStatus AcceptSubscriberEstablishment(const System::DeviceRuntimeIdentity& requester,StateSessionToken session,
                                                    StateVersion current) noexcept {
        std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
        auto* slot=FindSubscriberLocked(requester.Device);
        if(!slot) return StateRemoteStatus::NotFound;
        if(slot->Requester!=requester || slot->Session!=session) return StateRemoteStatus::SessionMismatch;
        if(slot->SessionState==StateRemoteSessionState::ActiveTrusted ||
           slot->SessionState==StateRemoteSessionState::ActiveNoBaseline) return StateRemoteStatus::Duplicate;
        if(slot->SessionState!=StateRemoteSessionState::Establishing) return StateRemoteStatus::Conflict;
        // Set publishes its latest version under this same table lock, including
        // while establishing. Prefer that fact over a caller snapshot taken before
        // a racing Set; otherwise acceptance can erase the only convergence wake.
        if(slot->LatestVersion) current=slot->LatestVersion;
        slot->HasAcceptedBaseline=slot->HasOfferedBaseline;
        slot->AcceptedBaseline=slot->HasOfferedBaseline?slot->OfferedBaseline:StateVersion{};
        // NoValue is a handshake result, not evidence for a fact committed while
        // establishment was in flight. That first fact still needs a baseline.
        slot->Dirty=bool(current) && (!slot->HasOfferedBaseline || current!=slot->OfferedBaseline);
        slot->LatestVersion=current;
        slot->SessionState=slot->HasOfferedBaseline?StateRemoteSessionState::ActiveTrusted:StateRemoteSessionState::ActiveNoBaseline;
        return StateRemoteStatus::Success;
    }
    StateRemoteStatus ActivateSubscriber(const System::DeviceRuntimeIdentity& requester,StateSessionToken session,bool hasBaseline,StateVersion baseline={}) noexcept {
        std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
        auto* slot=FindSubscriberLocked(requester.Device);
        if(!slot) return StateRemoteStatus::NotFound;
        if(slot->Requester!=requester || slot->Session!=session) return StateRemoteStatus::SessionMismatch;
        slot->HasAcceptedBaseline=hasBaseline;
        slot->AcceptedBaseline=hasBaseline?baseline:StateVersion{};
        slot->Dirty=false;
        slot->SessionState=hasBaseline?StateRemoteSessionState::ActiveTrusted:StateRemoteSessionState::ActiveNoBaseline;
        return StateRemoteStatus::Success;
    }
    void MarkLatestDirty(StateVersion current) noexcept {
        std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
        for(auto& slot:_subscribers) {
            if(!slot.Occupied) continue;
            slot.LatestVersion=current;
            if(slot.SessionState==StateRemoteSessionState::ResyncRequired) slot.ResyncRequiredPending=true;
            if(slot.SessionState!=StateRemoteSessionState::ActiveTrusted &&
               slot.SessionState!=StateRemoteSessionState::ActiveNoBaseline) continue;
            bool mustResync=false;
            if constexpr(RequiresAcknowledgement) {
                if(slot.HasAcceptedBaseline) {
                    const auto relation=CompareStateVersion(slot.AcceptedBaseline,current);
                    mustResync=relation==StateVersionRelation::Ambiguous || relation==StateVersionRelation::Older;
                }
            } else {
                mustResync=current.Revision==0;
            }
            if(mustResync) {
                slot.SessionState=StateRemoteSessionState::ResyncRequired;
                slot.ResyncRequiredPending=true;
                slot.Dirty=false;
                slot.Resync={};
            } else {
                slot.Dirty=true;
                slot.LatestVersion=current;
            }
        }
    }
    bool SubscriberDirty(const System::DeviceIdentifier& requester) const noexcept {
        std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
        const auto* slot=FindSubscriberLocked(requester);
        return slot?slot->Dirty:false;
    }
    StateRemoteSessionState SubscriberState(const System::DeviceIdentifier& requester) const noexcept {
        std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
        const auto* slot=FindSubscriberLocked(requester);
        return slot?slot->SessionState:StateRemoteSessionState::Inactive;
    }
    StateVersion SubscriberAcceptedBaseline(const System::DeviceIdentifier& requester) const noexcept {
        std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
        const auto* slot=FindSubscriberLocked(requester);
        return slot?slot->AcceptedBaseline:StateVersion{};
    }
    StateRemoteStatus AcceptSubscriberBaseline(const System::DeviceRuntimeIdentity& requester,StateSessionToken session,StateVersion version) noexcept {
        std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
        auto* slot=FindSubscriberLocked(requester.Device);
        if(!slot) return StateRemoteStatus::NotFound;
        if(slot->Requester!=requester || slot->Session!=session) return StateRemoteStatus::SessionMismatch;
        // Ordinary acceptance has no resync token. Only the matching ResyncAccepted
        // transaction can restore trust once continuity has been invalidated.
        if(slot->SessionState!=StateRemoteSessionState::ActiveTrusted &&
           slot->SessionState!=StateRemoteSessionState::ActiveNoBaseline)
            return StateRemoteStatus::SessionMismatch;
        if(slot->SessionState==StateRemoteSessionState::ActiveTrusted && slot->HasAcceptedBaseline &&
           slot->AcceptedBaseline==version) {
            slot->Dirty=bool(slot->LatestVersion) && slot->LatestVersion!=version;
            return StateRemoteStatus::Success;
        }
        if(!slot->HasOfferedBaseline || slot->OfferedBaseline!=version) return StateRemoteStatus::SessionMismatch;
        slot->AcceptedBaseline=version;
        slot->HasAcceptedBaseline=true;
        slot->HasPendingFirstBaseline=false;
        slot->Dirty=bool(slot->LatestVersion) && slot->LatestVersion!=version;
        slot->SessionState=StateRemoteSessionState::ActiveTrusted;
        return StateRemoteStatus::Success;
    }
    bool TryPrepareLatest(StateVersion current,StateSnapshot<TState>& snapshot,StateSourceWork& output) noexcept {
        if(!current) return false;
        std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
        for(auto& slot:_subscribers) {
            if(!slot.Occupied) continue;
            if(slot.SessionState==StateRemoteSessionState::ResyncRequired && slot.ResyncRequiredPending) {
                output={slot.Requester,slot.Session,StateMessageKind::ResyncRequired,slot.LatestVersion};
                return true;
            }
            if(!slot.Dirty) continue;
            // The canonical capture precedes this lock. A Set may already have
            // committed a newer version; never overwrite that marker with the
            // stale capture or let its completion clear the newer dirty truth.
            if(slot.SessionState==StateRemoteSessionState::ActiveNoBaseline && slot.HasPendingFirstBaseline) {
                StateStorageTraits<Value>::CopyOut(slot.ControlSnapshot.Value,snapshot.Value);
                snapshot.TruthTime=slot.ControlSnapshot.TruthTime;
                output={slot.Requester,slot.Session,StateMessageKind::BaselineSnapshot,slot.OfferedBaseline};
                return true;
            }
            if(slot.LatestVersion && slot.LatestVersion!=current) continue;
            StateMessageKind kind{};
            if(slot.SessionState==StateRemoteSessionState::ActiveNoBaseline) {
                kind=StateMessageKind::BaselineSnapshot;
                StateStorageTraits<Value>::CopyOut(snapshot.Value,slot.ControlSnapshot.Value);
                slot.ControlSnapshot.TruthTime=snapshot.TruthTime;
                slot.HasPendingFirstBaseline=true;
            } else if(slot.SessionState==StateRemoteSessionState::ActiveTrusted) kind=StateMessageKind::Publication;
            else continue;
            slot.OfferedBaseline=current;
            slot.HasOfferedBaseline=true;
            slot.LatestVersion=current;
            output={slot.Requester,slot.Session,kind,current};
            return true;
        }
        return false;
    }
    void CompleteLatestTransfer(const StateSourceWork& work,bool accepted) noexcept {
        std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
        auto* slot=FindSubscriberLocked(work.Requester.Device);
        if(!slot || slot->Requester!=work.Requester || slot->Session!=work.Session) return;
        if(work.Kind==StateMessageKind::ResyncRequired) {
            if(accepted && slot->SessionState==StateRemoteSessionState::ResyncRequired &&
               slot->LatestVersion==work.Version) slot->ResyncRequiredPending=false;
            return;
        }
        if(!slot->HasOfferedBaseline || slot->OfferedBaseline!=work.Version) return;
        if(accepted && slot->LatestVersion==work.Version) slot->Dirty=false;
    }
    StateRemoteStatus RemoveSubscriber(const System::DeviceRuntimeIdentity& requester,StateSessionToken session) noexcept {
        std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
        auto* slot=FindSubscriberLocked(requester.Device);
        if(!slot) return StateRemoteStatus::NotFound;
        if(slot->Requester!=requester || slot->Session!=session) return StateRemoteStatus::SessionMismatch;
        *slot={};
        return StateRemoteStatus::Success;
    }
    StateRemoteStatus RequireSubscriberResync(const System::DeviceIdentifier& requester) noexcept {
        std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
        auto* slot=FindSubscriberLocked(requester);
        if(!slot) return StateRemoteStatus::NotFound;
        slot->SessionState=StateRemoteSessionState::ResyncRequired;
        slot->ResyncRequiredPending=true;
        slot->Dirty=false;
        slot->Resync={};
        return StateRemoteStatus::Success;
    }
    StateRemoteStatus BeginSubscriberResync(const System::DeviceRuntimeIdentity& requester,StateSessionToken session,
                                            StateResyncToken token,StateVersion offered) noexcept {
        if(!token || !offered) return StateRemoteStatus::InvalidSession;
        std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
        auto* slot=FindSubscriberLocked(requester.Device);
        if(!slot) return StateRemoteStatus::NotFound;
        if(slot->Requester!=requester || slot->Session!=session) return StateRemoteStatus::SessionMismatch;
        if(slot->SessionState!=StateRemoteSessionState::ActiveTrusted &&
           slot->SessionState!=StateRemoteSessionState::ActiveNoBaseline &&
           slot->SessionState!=StateRemoteSessionState::ResyncRequired &&
           slot->SessionState!=StateRemoteSessionState::AwaitingResync) return StateRemoteStatus::Conflict;
        slot->Resync=token;
        slot->OfferedBaseline=offered;
        slot->HasOfferedBaseline=true;
        slot->Dirty=false;
        slot->SessionState=StateRemoteSessionState::AwaitingResync;
        return StateRemoteStatus::Success;
    }
    /// <summary>Captures a resync reply once per fresh requester token.</summary>
    /// <remarks>The one bounded control snapshot is reused after establishment.
    /// Retransmission of a token never changes its reply while its acceptance is pending.</remarks>
    StateRemoteStatus PrepareSubscriberResync(const System::DeviceRuntimeIdentity& requester,StateSessionToken session,
        StateResyncToken token,StateVersion& version,StateSnapshot<TState>& snapshot) noexcept {
        if(!token || !version) return StateRemoteStatus::InvalidSession;
        std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
        auto* slot=FindSubscriberLocked(requester.Device);
        if(!slot) return StateRemoteStatus::NotFound;
        if(slot->Requester!=requester || slot->Session!=session) return StateRemoteStatus::SessionMismatch;
        if(slot->SessionState==StateRemoteSessionState::Establishing ||
           slot->SessionState==StateRemoteSessionState::Inactive) return StateRemoteStatus::Conflict;
        if(token.Value()<slot->ResyncHighWater.Value()) return StateRemoteStatus::SessionMismatch;
        if(token==slot->ResyncHighWater) {
            if(slot->SessionState!=StateRemoteSessionState::AwaitingResync || slot->Resync!=token)
                return StateRemoteStatus::Duplicate;
            version=slot->OfferedBaseline;
            StateStorageTraits<Value>::CopyOut(slot->ControlSnapshot.Value,snapshot.Value);
            snapshot.TruthTime=slot->ControlSnapshot.TruthTime;
            return StateRemoteStatus::Success;
        }
        slot->ResyncHighWater=token;
        slot->Resync=token;
        slot->OfferedBaseline=version;
        StateStorageTraits<Value>::CopyOut(snapshot.Value,slot->ControlSnapshot.Value);
        slot->ControlSnapshot.TruthTime=snapshot.TruthTime;
        slot->HasOfferedBaseline=true;
        slot->Dirty=false;
        slot->SessionState=StateRemoteSessionState::AwaitingResync;
        return StateRemoteStatus::Success;
    }
    StateRemoteStatus AcceptSubscriberResync(const System::DeviceRuntimeIdentity& requester,StateSessionToken session,
                                             StateResyncToken token,StateVersion accepted,StateVersion current) noexcept {
        std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
        auto* slot=FindSubscriberLocked(requester.Device);
        if(!slot) return StateRemoteStatus::NotFound;
        if(slot->Requester!=requester || slot->Session!=session || slot->SessionState!=StateRemoteSessionState::AwaitingResync ||
           slot->Resync!=token || !slot->HasOfferedBaseline || slot->OfferedBaseline!=accepted)
            return StateRemoteStatus::SessionMismatch;
        slot->Resync={};
        slot->AcceptedBaseline=accepted;
        slot->HasAcceptedBaseline=true;
        if(slot->LatestVersion) current=slot->LatestVersion;
        slot->LatestVersion=current;
        slot->Dirty=current!=accepted;
        slot->SessionState=StateRemoteSessionState::ActiveTrusted;
        return StateRemoteStatus::Success;
    }
    std::size_t SubscribersInUse() const noexcept {
        std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
        std::size_t count=0;
        for(const auto& slot:_subscribers) if(slot.Occupied) ++count;
        return count;
    }
    StateConvergenceBindingView<TState> ConvergenceView() noexcept {
        return {this,[](void* p,StateVersion version) noexcept {
            static_cast<StateRemoteReplicaTable*>(p)->MarkLatestDirty(version);
        }};
    }
};

} // namespace ESPressio::State
