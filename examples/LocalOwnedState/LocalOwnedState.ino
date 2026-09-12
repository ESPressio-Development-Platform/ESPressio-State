#include <ESPressio_States.hpp>
#include <ESPressio_TypeDirectory.hpp>
#include <string_view>

using namespace ESPressio;
namespace S=ESPressio::State;

struct LedState final:S::State<LedState,bool> {
    static constexpr S::StateTypeId TypeId{0x1001};
    static constexpr std::string_view CanonicalName="Example.State.Led";
};

Primitive::TypeDirectory<1> directory;
S::Runtime<S::TypeConfiguration<LedState>> states;
S::StateOwner<LedState> owner;

void setup() {
    (void)directory.Register<LedState>();
    (void)directory.Initialize();
    owner=states.BindOwner<LedState>();
    (void)states.Initialize(directory.View());
    (void)states.Start();
    (void)owner.Set(true);
}

void loop() {}
