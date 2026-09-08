#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <mutex>
#include <tuple>
#include <utility>

#include <ESPressio_Synchronization.hpp>

#include "ESPressio_StateContract.hpp"
#include "ESPressio_StateRuntimeEpoch.hpp"

namespace ESPressio {
namespace State {

/// <summary>Controls whether local State registration lineage is retained when its source is unbound.</summary>
/**
 * ESPressio Memory Audit
 * Underlying storage: 1 bytes
 * Total Memory: 1 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
enum class StateUnbindMode : uint8_t {
    /// <summary>Remove the active source but preserve epoch/revision lineage for a later rebind.</summary>
    Retain = 0,
    /// <summary>Discard the current registration lineage so a later bind begins a new epoch.</summary>
    Discard
};

/// <summary>Read-only metadata describing one local State registration.</summary>
/**
 * ESPressio Memory Audit
 * Members:
 * - TypeId (StateTypeId): 8 bytes [0 bytes dynamic allocation]
 * - Epoch (StateEpoch): 4 bytes [0 bytes dynamic allocation]
 * - Revision (StateRevision): 8 bytes [0 bytes dynamic allocation]
 * - Bound (bool): 1 bytes [0 bytes dynamic allocation]
 * - Retained (bool): 1 bytes [0 bytes dynamic allocation]
 * Total Memory: 24 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
struct LocalStateRegistrationSnapshot final {
    StateTypeId TypeId = 0;
    StateEpoch Epoch = 0;
    StateRevision Revision = 0;
    bool Bound = false;
    bool Retained = false;
};

/// <summary>Read-only non-owning view over an application-owned authoritative State value.</summary>
/**
 * ESPressio Memory Audit
 * Members:
 * - ValueSource (Value*): 4 bytes [0 bytes dynamic allocation]
 * - Epoch (StateEpoch): 4 bytes [0 bytes dynamic allocation]
 * - Revision (StateRevision): 8 bytes [0 bytes dynamic allocation]
 * Total Memory: 16 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
template<typename TDefinition>
struct LocalStateView final {
    using Value = StateValueType<TDefinition>;
    const Value* ValueSource = nullptr;
    StateEpoch Epoch = 0;
    StateRevision Revision = 0;

    explicit operator bool() const noexcept { return ValueSource != nullptr; }
    const Value& ValueRef() const { return *ValueSource; }
};

/// <summary>
/// Binds application-owned authoritative State values without copying them into a central repository.
/// </summary>
/// <typeparam name="TContract">Closed set of State definitions supported by the registry.</typeparam>
/// <remarks>
/// The application remains responsible for the lifetime and mutation of every bound value. The registry
/// exposes only const views, owns each State registration's epoch/revision lineage, rejects duplicate
/// authority, and requires NotifyChanged to advance the distributed revision after authoritative mutation.
/// Initial binding establishes revision 1. Rebinding a retained registration advances the existing
/// revision before exposing the new source so the same epoch/revision can never denote two different values.
/// A configured StateRuntimeEpoch is captured when the registry is constructed and becomes the first epoch
/// used by each State definition; without one, the historical epoch-one start remains unchanged.
/// </remarks>
/**
 * ESPressio Memory Audit
 * Members:
 * - _slots (SlotTuple<TContract>::Type): sizeof(SlotTuple<TContract>::Type) (target/toolchain dependent) [0 bytes dynamic allocation]
 * - _runtimeInitialEpoch (StateEpoch): 4 bytes [0 bytes dynamic allocation]
 * - _mutex (System::Synchronization::RecursiveMutex): 20 bytes [_owned: owned object: 4 bytes; _fallback: _mutex: native synchronization state may allocate platform resources lazily]
 * Total Memory: 24 bytes known/aligned storage + sizeof(SlotTuple<TContract>::Type) (target/toolchain dependent) [_mutex: _owned: owned object: 4 bytes; _mutex: _fallback: _mutex: native synchronization state may allocate platform resources lazily]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * Confidence: low; compile-time sizeof on the concrete target remains authoritative for ABI-sensitive/opaque members.
 * End ESPressio Memory Audit
 */
template<typename TContract>
class LocalStateRegistry final {
private:
        /**
     * ESPressio Memory Audit
     * Members:
     * - Source (StateValueType<TDefinition>*): 4 bytes [0 bytes dynamic allocation]
     * - Epoch (StateEpoch): 4 bytes [0 bytes dynamic allocation]
     * - Revision (StateRevision): 8 bytes [0 bytes dynamic allocation]
     * - Bound (bool): 1 bytes [0 bytes dynamic allocation]
     * - Retained (bool): 1 bytes [0 bytes dynamic allocation]
     * - NeedsNewEpoch (bool): 1 bytes [0 bytes dynamic allocation]
     * Total Memory: 20 bytes [0 bytes dynamic allocation]
     * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
     * End ESPressio Memory Audit
     */
template<typename TDefinition>
    struct Slot final {
        StateValueType<TDefinition>* Source = nullptr;
        StateEpoch Epoch = 0;
        StateRevision Revision = 0;
        bool Bound = false;
        bool Retained = false;
        bool NeedsNewEpoch = true;
    };

    template<typename T>
    struct SlotTuple;

        /**
     * ESPressio Memory Audit
     * Members: none (standalone empty object occupies 1 byte; an eligible empty base may be optimized to 0 bytes).
     * Total Memory: 1 bytes [0 bytes dynamic allocation]
     * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
     * End ESPressio Memory Audit
     */
template<typename... TDefinitions>
    struct SlotTuple<StateContract<TDefinitions...>> {
        using Type = std::tuple<Slot<TDefinitions>...>;
    };

    typename SlotTuple<TContract>::Type _slots{};
    StateEpoch _runtimeInitialEpoch{StateRuntimeEpoch::Current()};
    mutable System::Synchronization::RecursiveMutex _mutex;

    static StateEpoch NextEpoch(StateEpoch current) noexcept {
        if (current == 0 || current == std::numeric_limits<StateEpoch>::max()) return 1;
        return static_cast<StateEpoch>(current + 1);
    }

    static bool AdvanceRevision(StateRevision& revision) noexcept {
        if (revision == std::numeric_limits<StateRevision>::max()) return false;
        ++revision;
        if (revision == 0) return false;
        return true;
    }

    template<typename TDefinition>
    Slot<TDefinition>& GetSlot() {
        static_assert(TContract::template Contains<TDefinition>, "State definition is not part of this StateContract");
        return std::get<TContract::template IndexOf<TDefinition>()>(_slots);
    }

    template<typename TDefinition>
    const Slot<TDefinition>& GetSlot() const {
        static_assert(TContract::template Contains<TDefinition>, "State definition is not part of this StateContract");
        return std::get<TContract::template IndexOf<TDefinition>()>(_slots);
    }

public:
    /// <summary>Move-only RAII owner for one local State binding.</summary>
        /**
     * ESPressio Memory Audit
     * Members:
     * - _registry (LocalStateRegistry*): 4 bytes [0 bytes dynamic allocation]
     * - _onDestroy (StateUnbindMode): 1 bytes [0 bytes dynamic allocation]
     * Total Memory: 8 bytes [0 bytes dynamic allocation]
     * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
     * End ESPressio Memory Audit
     */
template<typename TDefinition>
    class Binding final {
    private:
        LocalStateRegistry* _registry = nullptr;
        StateUnbindMode _onDestroy = StateUnbindMode::Retain;

    public:
        Binding() = default;
        Binding(LocalStateRegistry& registry, StateUnbindMode onDestroy) noexcept
            : _registry(&registry), _onDestroy(onDestroy) {}
        Binding(const Binding&) = delete;
        Binding& operator=(const Binding&) = delete;
        Binding(Binding&& other) noexcept
            : _registry(other._registry), _onDestroy(other._onDestroy) {
            other._registry = nullptr;
        }
        Binding& operator=(Binding&& other) noexcept {
            if (this == &other) return *this;
            Reset();
            _registry = other._registry;
            _onDestroy = other._onDestroy;
            other._registry = nullptr;
            return *this;
        }
        ~Binding() { Reset(); }

        explicit operator bool() const noexcept { return _registry != nullptr; }

        /// <summary>Unbinds immediately using the explicitly selected lifecycle mode.</summary>
        void Reset() noexcept {
            if (_registry != nullptr) {
                _registry->template Unbind<TDefinition>(_onDestroy);
                _registry = nullptr;
            }
        }
    };

    /// <summary>Binds one application-owned value as the sole local authority for its State definition.</summary>
    /// <remarks>
    /// A new lineage begins at the configured runtime epoch (or epoch one when no runtime epoch was configured) and
    /// revision 1. Rebinding after Retain preserves the epoch and advances the revision once before exposing the
    /// replacement source, preventing an old revision from acquiring a different value. Binding fails if the revision
    /// space is exhausted. A later Discard still advances to a fresh epoch from the current lineage.
    /// </remarks>
    template<typename TDefinition>
    bool Bind(StateValueType<TDefinition>& source) {
        std::lock_guard<System::Synchronization::RecursiveMutex> lock(_mutex);
        auto& slot = GetSlot<TDefinition>();
        if (slot.Bound) return false;

        if (slot.NeedsNewEpoch) {
            slot.Epoch = slot.Epoch == 0U && _runtimeInitialEpoch != 0U
                ? _runtimeInitialEpoch
                : NextEpoch(slot.Epoch);
            slot.Revision = 1;
            slot.Retained = false;
            slot.NeedsNewEpoch = false;
        } else {
            if (!AdvanceRevision(slot.Revision)) return false;
            slot.Retained = false;
        }

        slot.Source = &source;
        slot.Bound = true;
        return true;
    }

    /// <summary>Binds one source and returns a move-only RAII binding that unbinds using the requested mode.</summary>
    template<typename TDefinition>
    Binding<TDefinition> BindScoped(StateValueType<TDefinition>& source, StateUnbindMode onDestroy) {
        if (!Bind<TDefinition>(source)) return {};
        return Binding<TDefinition>(*this, onDestroy);
    }

    /// <summary>Removes one authoritative source while either retaining or discarding its registration lineage.</summary>
    template<typename TDefinition>
    bool Unbind(StateUnbindMode mode) noexcept {
        std::lock_guard<System::Synchronization::RecursiveMutex> lock(_mutex);
        auto& slot = GetSlot<TDefinition>();
        if (!slot.Bound) return false;
        slot.Source = nullptr;
        slot.Bound = false;
        if (mode == StateUnbindMode::Retain) {
            slot.Retained = true;
        } else {
            slot.Retained = false;
            slot.Revision = 0;
            slot.NeedsNewEpoch = true;
        }
        return true;
    }

    /// <summary>Advances the revision after the application has authoritatively changed the bound value.</summary>
    /// <remarks>NotifyChanged is explicit authority and therefore advances revision regardless of equality policy.</remarks>
    template<typename TDefinition>
    bool NotifyChanged(StateRevision& revision) noexcept {
        std::lock_guard<System::Synchronization::RecursiveMutex> lock(_mutex);
        auto& slot = GetSlot<TDefinition>();
        if (!slot.Bound || slot.Source == nullptr) return false;
        if (!AdvanceRevision(slot.Revision)) return false;
        revision = slot.Revision;
        return true;
    }

    /// <summary>Returns a read-only non-owning view of the currently bound authoritative value.</summary>
    template<typename TDefinition>
    bool Read(LocalStateView<TDefinition>& view) const noexcept {
        std::lock_guard<System::Synchronization::RecursiveMutex> lock(_mutex);
        const auto& slot = GetSlot<TDefinition>();
        if (!slot.Bound || slot.Source == nullptr) return false;
        view.ValueSource = slot.Source;
        view.Epoch = slot.Epoch;
        view.Revision = slot.Revision;
        return true;
    }

    /// <summary>Reads registration metadata even when a retained registration is currently unbound.</summary>
    template<typename TDefinition>
    LocalStateRegistrationSnapshot Registration() const noexcept {
        std::lock_guard<System::Synchronization::RecursiveMutex> lock(_mutex);
        const auto& slot = GetSlot<TDefinition>();
        return {
            StateTypeIdOf<TDefinition>,
            slot.Epoch,
            slot.Revision,
            slot.Bound,
            slot.Retained
        };
    }
};

} // namespace State
} // namespace ESPressio
