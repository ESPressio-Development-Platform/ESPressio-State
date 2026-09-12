#include <ESPressio_States.hpp>
#include <ESPressio_TypeDirectory.hpp>
#include <string_view>
struct BadValue { BadValue() noexcept=default; BadValue(const BadValue&) noexcept(false){} BadValue& operator=(const BadValue&) noexcept {return *this;} bool operator==(const BadValue&) const noexcept{return true;} };
struct InvalidStorageState final : ESPressio::State::State<InvalidStorageState,BadValue> {
    static constexpr ESPressio::State::StateTypeId TypeId{4001};
    static constexpr std::string_view CanonicalName="Invalid.State.Storage";
};
int main(){ ESPressio::Primitive::TypeDirectory<1> directory; (void)directory.Register<InvalidStorageState>(); }
