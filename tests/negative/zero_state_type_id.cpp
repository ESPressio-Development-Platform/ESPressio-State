#include <ESPressio_States.hpp>
#include <ESPressio_TypeDirectory.hpp>
#include <string_view>
struct InvalidState final : ESPressio::State::State<InvalidState,int> {
    static constexpr ESPressio::State::StateTypeId TypeId{0};
    static constexpr std::string_view CanonicalName="Invalid.State.Zero";
};
int main(){ ESPressio::Primitive::TypeDirectory<1> directory; (void)directory.Register<InvalidState>(); }
