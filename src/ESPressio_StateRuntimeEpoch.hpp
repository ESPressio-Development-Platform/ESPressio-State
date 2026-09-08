#pragma once

#include <atomic>

#include "ESPressio_StateContract.hpp"

namespace ESPressio {
namespace State {

/// <summary>Boot/runtime-scoped seed for the first lineage of newly constructed local State registries.</summary>
/// <remarks>
/// Distributed State revisions are meaningful only inside an epoch. A process/device restart therefore needs a fresh
/// epoch before it can safely restart revisions at one. Composition may configure one non-zero runtime epoch before
/// constructing StatePublisher/LocalStateRegistry objects. Existing applications that do not configure this service
/// preserve the historical behaviour where a fresh registry starts at epoch one.
///
/// This service deliberately does not generate or persist epochs. Entropy, boot identity and persistence are platform /
/// composition responsibilities. Configure is idempotent for the same value and rejects changing the runtime epoch
/// after it has been established, preventing independently constructed publishers from silently entering different
/// revision domains during one runtime.
/// </remarks>
/**
 * ESPressio Memory Audit
 * Members: none (standalone empty object occupies 1 byte; an eligible empty base may be optimized to 0 bytes).
 * Total Memory: 1 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
class StateRuntimeEpoch final {
    static std::atomic<StateEpoch>& Storage() noexcept {
        static std::atomic<StateEpoch> epoch{0U};
        return epoch;
    }

public:
    StateRuntimeEpoch() = delete;

    static bool Configure(StateEpoch epoch) noexcept {
        if (epoch == 0U) return false;
        auto& storage = Storage();
        StateEpoch expected = 0U;
        if (storage.compare_exchange_strong(
                expected, epoch, std::memory_order_acq_rel, std::memory_order_acquire)) {
            return true;
        }
        return expected == epoch;
    }

    static StateEpoch Current() noexcept {
        return Storage().load(std::memory_order_acquire);
    }
};

} // namespace State
} // namespace ESPressio
