#include <ESPressio_States.hpp>
#include <ESPressio_ThreadWith.hpp>
#include <ESPressio_TypeDirectory.hpp>
#include <string_view>

using namespace ESPressio;
namespace S=ESPressio::State;

struct TemperatureState final:S::State<TemperatureState,int> {
    static constexpr S::StateTypeId TypeId{0x1101};
    static constexpr std::string_view CanonicalName="Example.State.ObservedTemperature";
};

Primitive::TypeDirectory<1> directory;
S::Runtime<S::TypeConfiguration<TemperatureState>> states;
S::StateOwner<TemperatureState> owner;

class StateWorker final:public Threads::ThreadWith<S::ObserverCapability<TemperatureState>> {
protected:
    Threads::ThreadWorkDisposition OnLoop() override {
        return Threads::ThreadWorkDisposition::IdleReady;
    }
public:
    StateWorker() {
        (void)GetCapability<S::ObserverCapabilityTag>().OnChange(*this,&StateWorker::OnStateChanged);
    }
private:
    void OnStateChanged(const S::StateChangeSet& changes) {
        if(!changes.Contains<TemperatureState>()) return;
        S::StateSnapshot<TemperatureState> snapshot{};
        (void)states.TryRead(snapshot); // application code runs here, never inside StateOwner::Set().
    }
};

StateWorker worker;

void setup() {
    (void)directory.Register<TemperatureState>();
    (void)directory.Initialize();
    owner=states.BindOwner<TemperatureState>();
    (void)states.Initialize(directory.View());

    // The observer capability stages its frozen target before State starts.
    // A concrete System execution provider must already be installed by the platform.
    (void)worker.Initialize();
    (void)states.Start();
    (void)worker.Start();

    (void)owner.Set(24); // only a pending bit + the Thread's common Wake are published.
}

void loop() {}
