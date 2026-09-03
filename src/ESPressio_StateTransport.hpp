#pragma once

#include <cstddef>
#include <cstdint>

#include "ESPressio_DeviceIdentifier.hpp"
#include "ESPressio_StateContract.hpp"

namespace ESPressio {
namespace State {

/// <summary>Transport metadata that uniquely identifies one authoritative State revision.</summary>
struct StateUpdateHeader {
    /// <summary>Permanent device identity of the authoritative State source.</summary>
    DeviceIdentifier Origin{};
    /// <summary>Stable State type identifier.</summary>
    StateTypeId TypeId = 0;
    /// <summary>State epoch associated with the publication lineage.</summary>
    StateEpoch Epoch = 0;
    /// <summary>Monotonic revision within the epoch.</summary>
    StateRevision Revision = 0;
};

/// <summary>Combines State transport metadata with one strongly typed authoritative value.</summary>
/// <typeparam name="TValue">State value type.</typeparam>
template<typename TValue>
struct StateUpdate final {
    /// <summary>Transport metadata for the publication.</summary>
    StateUpdateHeader Header{};
    /// <summary>Authoritative State value represented by this revision.</summary>
    TValue Value{};
};

/// <summary>
/// Retains only the latest committed publication for one State publication target.
/// </summary>
/// <typeparam name="TValue">Published State value type.</typeparam>
/// <remarks>
/// State replication is latest-fact propagation, not an acknowledgement-driven
/// RPC exchange. A newer epoch/revision supersedes older retained work; there is
/// no baseline replica acknowledgement lifecycle.
/// </remarks>
template<typename TValue>
struct PendingStateUpdate final {
    /// <summary>Indicates whether a publication is currently retained for transport.</summary>
    bool Pending = false;
    /// <summary>Epoch of the latest retained publication.</summary>
    StateEpoch Epoch = 0;
    /// <summary>Revision of the latest retained publication.</summary>
    StateRevision Revision = 0;
    /// <summary>Latest retained authoritative value.</summary>
    TValue Value{};

    /// <summary>Replaces retained work when the supplied epoch/revision is newer.</summary>
    bool Replace(StateEpoch epoch, StateRevision revision, const TValue& value) {
        if (revision == 0) return false;
        if (Pending && epoch < Epoch) return false;
        if (Pending && epoch == Epoch && revision <= Revision) return false;
        Epoch = epoch;
        Revision = revision;
        Value = value;
        Pending = true;
        return true;
    }

    /// <summary>Clears retained transport work after the transport owner has consumed it.</summary>
    void Clear() noexcept { Pending = false; }
};

} // namespace State
} // namespace ESPressio
