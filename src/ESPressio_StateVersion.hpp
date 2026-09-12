#pragma once
#include <cstdint>
namespace ESPressio::State {
struct StateVersion final {
    bool Phase=false;
    std::uint16_t Revision=0;
    constexpr explicit operator bool() const noexcept { return Revision!=0 || Phase; }
    constexpr bool operator==(const StateVersion& other) const noexcept { return Phase==other.Phase && Revision==other.Revision; }
    constexpr bool operator!=(const StateVersion& other) const noexcept { return !(*this==other); }
};
enum class StateVersionRelation : std::uint8_t { Duplicate,Newer,Ambiguous,Older };
constexpr std::uint32_t StateVersionSerial(StateVersion value) noexcept {
    return (static_cast<std::uint32_t>(value.Phase)<<16) | value.Revision;
}
constexpr StateVersion NextStateVersion(StateVersion current,bool hasValue) noexcept {
    if(!hasValue) return {false,1};
    if(current.Revision==UINT16_MAX) return {!current.Phase,0};
    return {current.Phase,static_cast<std::uint16_t>(current.Revision+1)};
}
constexpr StateVersionRelation CompareStateVersion(StateVersion accepted,StateVersion incoming) noexcept {
    constexpr std::uint32_t Mask=(1u<<17)-1u;
    const auto delta=(StateVersionSerial(incoming)-StateVersionSerial(accepted)) & Mask;
    if(delta==0) return StateVersionRelation::Duplicate;
    if(delta<65536u) return StateVersionRelation::Newer;
    if(delta==65536u) return StateVersionRelation::Ambiguous;
    return StateVersionRelation::Older;
}
}
