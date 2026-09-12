#include <ESPressio_States.hpp>
#include <ESPressio_TypeDirectory.hpp>
#include <ESPressio_ThreadCapability.hpp>
#include <cassert>
#include <string_view>
using namespace ESPressio;
namespace S=ESPressio::State;

struct ObservedA final : S::State<ObservedA,int> {
    static constexpr S::StateTypeId TypeId{1101};
    static constexpr std::string_view CanonicalName="Test.State.ObservedA";
};
struct ObservedB final : S::State<ObservedB,int> {
    static constexpr S::StateTypeId TypeId{1102};
    static constexpr std::string_view CanonicalName="Test.State.ObservedB";
};

static Timing::QualifiedTime Captured(){ return {100,Timing::TimeReliability::Synchronized}; }

struct Host final {
    unsigned Wakes=0;
    bool Accepting=true;
    static bool Wake(void* p,bool) noexcept { ++static_cast<Host*>(p)->Wakes;return true; }
    static bool Accepts(const void* p) noexcept { return static_cast<const Host*>(p)->Accepting; }
};

struct Sink final {
    S::StateOwner<ObservedA>* OwnerA=nullptr;
    int Calls=0;
    bool SawA=false;
    bool SawB=false;
    bool MutateDuringCallback=false;
    void Changed(const S::StateChangeSet& changes){
        ++Calls;
        SawA=changes.Contains<ObservedA>();
        SawB=changes.Contains<ObservedB>();
        if(MutateDuringCallback){
            MutateDuringCallback=false;
            assert(OwnerA && OwnerA->Set(3,{300,Timing::TimeReliability::Holdover})==S::StateSetStatus::Changed);
        }
    }
};

int main(){
    using Capability=S::ObserverCapability<ObservedA,ObservedB>;
    static_assert(Threads::Internal::ValidCapability<Capability>::value);
    static_assert(Capability::ObservationCount==2);
    static_assert(Capability::PendingBitmapBytes==sizeof(std::uint64_t));

    // Local State must not require P4 distributed identity.
    Primitive::TypeDirectory<2> directory;
    assert(directory.Register<ObservedA>()==Primitive::TypeDirectoryRegistrationStatus::Success);
    assert(directory.Register<ObservedB>()==Primitive::TypeDirectoryRegistrationStatus::Success);
    assert(directory.Initialize()==Primitive::TypeDirectoryInitializationStatus::Success);

    using Runtime=S::Runtime<S::TypeConfiguration<ObservedA>,S::TypeConfiguration<ObservedB>>;
    Runtime runtime;
    auto ownerA=runtime.BindOwner<ObservedA>();assert(ownerA);
    auto ownerB=runtime.BindOwner<ObservedB>();assert(ownerB);
    assert(runtime.Initialize(directory.View(),&Captured)==S::StateRuntimeStatus::Success);

    Host host;
    Threads::ThreadHostServices services{};
    services.Owner=&host;
    services.WakeFunction=&Host::Wake;
    services.AcceptingFunction=&Host::Accepts;

    Sink sink;sink.OwnerA=&ownerA;
    Capability capability;
    assert(capability.OnChange(sink,&Sink::Changed));
    assert(capability.Initialize(services)==Threads::ThreadStatus::Success);

    // State topology cannot freeze an observer that has not finalized its own configuration.
    assert(runtime.Start()==S::StateRuntimeStatus::InvalidConfiguration);
    assert(capability.FinalizeInitialization()==Threads::ThreadStatus::Success);
    assert(runtime.Start()==S::StateRuntimeStatus::Success);
    capability.Activate({0,services});
    assert(!capability.HasPending() && host.Wakes==0);

    // Repeated changes before service coalesce to one identity bit and one common Wake.
    assert(ownerA.Set(1,{101,Timing::TimeReliability::Synchronized})==S::StateSetStatus::Changed);
    assert(ownerA.Set(2,{102,Timing::TimeReliability::Synchronized})==S::StateSetStatus::Changed);
    assert(capability.HasPending() && host.Wakes==1);
    assert(capability.Readiness({0,services}).Immediate);
    capability.Service({0,services});
    assert(sink.Calls==1 && sink.SawA && !sink.SawB);
    S::StateSnapshot<ObservedA> snapshot{};
    assert(runtime.TryRead(snapshot) && snapshot.Value==2 && snapshot.TruthTime.Nanoseconds==102);

    // Equal candidate is a strict no-op: no truth-time refresh, bit or wake.
    assert(ownerA.Set(2,{999,Timing::TimeReliability::Unqualified})==S::StateSetStatus::NoChange);
    assert(!capability.HasPending() && host.Wakes==1);
    assert(runtime.TryRead(snapshot) && snapshot.TruthTime.Nanoseconds==102);

    // A producer racing during the application callback survives for a later quantum.
    sink.MutateDuringCallback=true;
    assert(ownerB.Set(9,{200,Timing::TimeReliability::Acquiring})==S::StateSetStatus::Changed);
    assert(host.Wakes==2);
    capability.Service({0,services});
    assert(sink.Calls==2 && !sink.SawA && sink.SawB);
    assert(capability.HasPending());
    assert(host.Wakes==3);
    capability.Service({0,services});
    assert(sink.Calls==3 && sink.SawA && !sink.SawB);
    assert(!capability.HasPending());

    // Pause does not discard latest-truth pending identity.
    capability.Pause({0,services});
    assert(ownerB.Set(10,{201,Timing::TimeReliability::Holdover})==S::StateSetStatus::Changed);
    assert(capability.HasPending());
    capability.Activate({0,services});
    capability.Service({0,services});
    assert(sink.Calls==4 && sink.SawB);

    // Quiescence detaches the producer target; no application callback can be generated afterward.
    capability.Quiesce({0,services});
    const auto calls=sink.Calls;
    const auto wakes=host.Wakes;
    assert(ownerA.Set(4,{400,Timing::TimeReliability::Synchronized})==S::StateSetStatus::Changed);
    assert(!capability.HasPending() && host.Wakes==wakes && sink.Calls==calls);

    assert(runtime.Shutdown()==S::StateRuntimeStatus::Success);
}
