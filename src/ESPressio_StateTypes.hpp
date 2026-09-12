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

/// <summary>Requester-runtime scoped, non-zero State subscription-session identity.</summary>
class StateSessionToken final {
    std::uint32_t _value=0;
public:
    constexpr StateSessionToken() noexcept=default;
    constexpr explicit StateSessionToken(std::uint32_t value) noexcept:_value(value){}
    constexpr std::uint32_t Value() const noexcept { return _value; }
    constexpr explicit operator bool() const noexcept { return _value!=0; }
    constexpr bool operator==(StateSessionToken other) const noexcept { return _value==other._value; }
    constexpr bool operator!=(StateSessionToken other) const noexcept { return _value!=other._value; }
};

/// <summary>Fresh non-zero identity for one resynchronization request/retry.</summary>
class StateResyncToken final {
    std::uint32_t _value=0;
public:
    constexpr StateResyncToken() noexcept=default;
    constexpr explicit StateResyncToken(std::uint32_t value) noexcept:_value(value){}
    constexpr std::uint32_t Value() const noexcept { return _value; }
    constexpr explicit operator bool() const noexcept { return _value!=0; }
    constexpr bool operator==(StateResyncToken other) const noexcept { return _value==other._value; }
    constexpr bool operator!=(StateResyncToken other) const noexcept { return _value!=other._value; }
};

static_assert(sizeof(StateSessionToken)==sizeof(std::uint32_t),"StateSessionToken must remain an exact 32-bit semantic value");
static_assert(sizeof(StateResyncToken)==sizeof(std::uint32_t),"StateResyncToken must remain an exact 32-bit semantic value");

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
template<class T,class=void> struct HasConvergencePolicy : std::false_type {};
template<class T> struct HasConvergencePolicy<T,std::void_t<typename T::ConvergencePolicy>> : std::true_type {};
}
}
