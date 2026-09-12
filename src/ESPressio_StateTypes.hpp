#pragma once
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <ESPressio_PrimitiveTypeId.hpp>
#include <ESPressio_TimeReliability.hpp>

namespace ESPressio::State {
using StateTypeId = Primitive::StateTypeId;
inline constexpr Primitive::PrimitiveFamilyId StateFamilyId = Primitive::FamilyIds::State;
inline constexpr Primitive::PrimitiveProtocolVersion StateProtocolVersion = 1;

enum class StateTier : std::uint8_t { Local=0, Serializable=1, Transmissible=2 };
enum class StateSetStatus : std::uint8_t {
    Changed,
    NoChange,
    NotRunning,
    OwnerUnavailable,
    PersistenceFailed,
    StoragePreparationFailed
};
enum class StateRuntimeStatus : std::uint8_t {
    Success,
    AlreadyInitialized,
    NotInitialized,
    InvalidConfiguration,
    InvalidDirectory,
    TypeConflict,
    IdentityUnavailable,
    Frozen,
    Stopping
};

template<class TValue>
struct StateStorageTraits {
    static constexpr bool Supported =
        std::is_nothrow_default_constructible_v<TValue> &&
        std::is_nothrow_copy_constructible_v<TValue> &&
        std::is_nothrow_copy_assignable_v<TValue> &&
        std::is_nothrow_destructible_v<TValue>;
    static bool Prepare(const TValue& candidate,TValue& prepared) noexcept {
        prepared=candidate;
        return true;
    }
    static void Commit(TValue& canonical,const TValue& prepared) noexcept { canonical=prepared; }
    static void CopyOut(const TValue& canonical,TValue& output) noexcept { output=canonical; }
};

namespace Detail {
template<class T,class=void> struct HasCanonicalName : std::false_type {};
template<class T> struct HasCanonicalName<T,std::void_t<decltype(T::CanonicalName)>> : std::true_type {};
}
}
