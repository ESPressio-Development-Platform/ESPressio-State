#pragma once
#include <atomic>
#include <cstddef>

namespace ESPressio::State {

/// <summary>Intrusive, runtime-owned registration record for one State observation endpoint.</summary>
/// <remarks>The observing capability owns the node storage. StateTypeRuntime only links the borrowed
/// node while the capability is initialized. Publication invokes only fixed non-application thunks.</remarks>
struct StateObserverTargetNode final {
    StateObserverTargetNode* Next=nullptr;
    void* Owner=nullptr;
    std::size_t ObservationIndex=0;
    bool (*Validate)(const void*) noexcept=nullptr;
    void (*Publish)(void*,std::size_t) noexcept=nullptr;
    std::atomic<bool> Linked{false};

    explicit operator bool() const noexcept {
        return Owner && Validate && Publish;
    }
};

} // namespace ESPressio::State
