#include <ESPressio_States.hpp>
#include <ESPressio_RuntimeIdentity.hpp>
#include <ESPressio_SerializationMacros.hpp>
#include <ESPressio_TypeDirectory.hpp>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <future>
#include <mutex>
#include <thread>
using namespace ESPressio;
namespace S=ESPressio::State;
using namespace std::chrono_literals;

struct Gate final {
    std::mutex Mutex;
    std::condition_variable Changed;
    bool Entered=false,Released=false;
    void Enter() {
        std::unique_lock<std::mutex> lock(Mutex);Entered=true;Changed.notify_all();
        Changed.wait(lock,[&]{return Released;});
    }
    void WaitEntered() {
        std::unique_lock<std::mutex> lock(Mutex);
        assert(Changed.wait_for(lock,2s,[&]{return Entered;}));
    }
    void Release() { std::lock_guard<std::mutex> lock(Mutex);Released=true;Changed.notify_all(); }
};
struct Local final : S::State<Local,int> {
    static constexpr S::StateTypeId TypeId{0x5901};
    static constexpr std::string_view CanonicalName="Test.State.ShutdownLocal";
};
namespace ESPressio::State {
template<> struct StateComparison<Local> {
    inline static Gate* Block=nullptr;
    static bool Equals(const int& a,const int& b) noexcept { if(Block) Block->Enter();return a==b; }
};
}
struct Value final {
    std::uint32_t Number=0;
    bool operator==(const Value& other) const noexcept { return Number==other.Number; }
    ESPRESSIO_SERIALIZABLE_TYPE(Value)
    ESPRESSIO_SERIALIZABLE_SCHEMA_VERSION(1)
    ESPRESSIO_SERIALIZABLE_PROPERTIES(ESPRESSIO_PROPERTY("number",Number))
};
struct Policy final {
    using PolicyCategory=Primitive::StateConvergencePolicyTag;
    using RequiredEvidence=Primitive::DestinationPrimitiveAdmission;
    using Supersession=Primitive::LatestAuthoritativeValue;
    using ExhaustionDisposition=Primitive::DormantNeedsConvergence;
    static constexpr std::uint64_t MaximumResidenceNanoseconds=1000000000;
    static constexpr std::uint64_t MaximumAdapterAdmissionWaitNanoseconds=1000000;
    static constexpr std::uint16_t MaximumAttempts=3;
    static constexpr std::uint64_t MinimumRetrySpacingNanoseconds=1000;
    static constexpr std::uint64_t MaximumRetrySpacingNanoseconds=1000000;
};
struct Remote final : S::TransmissibleState<Remote,Value> {
    static constexpr S::StateTypeId TypeId{0x5902};
    static constexpr std::string_view CanonicalName="Test.State.ShutdownRemote";
    using ConvergencePolicy=Policy;
};
struct Adapter final {
    unsigned Wakes=0;
    void Wake() noexcept { ++Wakes; }
    Gate* Block=nullptr;
    bool Validate(const S::StateTransportContract&) noexcept { return true; }
    S::StateTransportAdmission Admit(const S::StateOutboundMessage<Remote>&) noexcept {
        if(Block) Block->Enter();
        return {S::StateTransportAdmissionStatus::Accepted};
    }
};
static System::DeviceRuntimeIdentity Identity(std::uint8_t marker) {
    System::DeviceIdentifier::Storage bytes{};bytes[0]=marker;
    return {System::DeviceIdentifier{bytes},System::RuntimeIncarnationId{1}};
}
static Timing::QualifiedTime Capture(){return {1,Timing::TimeReliability::Synchronized};}
template<class Runtime>
static void WaitClosed(Runtime& runtime) {
    const auto deadline=std::chrono::steady_clock::now()+2s;
    while(runtime.IsRunning() && std::chrono::steady_clock::now()<deadline) std::this_thread::yield();
    assert(!runtime.IsRunning());
}
int main(){
    assert(System::RuntimeIdentity::Install(Identity(1))==System::RuntimeIdentity::InstallationStatus::Success);
    Primitive::TypeDirectory<2> directory;
    assert(directory.Register<Local>()==Primitive::TypeDirectoryRegistrationStatus::Success);
    assert(directory.Register<Remote>()==Primitive::TypeDirectoryRegistrationStatus::Success);
    assert(directory.Initialize()==Primitive::TypeDirectoryInitializationStatus::Success);
    S::Runtime<S::TypeConfiguration<Local>> local;
    auto owner=local.BindOwner<Local>();assert(owner);
    assert(local.Initialize(directory.View(),&Capture)==S::StateRuntimeStatus::Success);
    assert(local.Start()==S::StateRuntimeStatus::Success);
    assert(owner.Set(1)==S::StateSetStatus::Changed);
    Gate commit;
    S::StateComparison<Local>::Block=&commit;
    auto setter=std::async(std::launch::async,[&]{return owner.Set(2);});
    commit.WaitEntered();
    auto stopped=std::async(std::launch::async,[&]{return local.Shutdown();});
    WaitClosed(local);
    assert(stopped.wait_for(20ms)==std::future_status::timeout);
    commit.Release();
    assert(setter.get()==S::StateSetStatus::Changed);
    assert(stopped.get()==S::StateRuntimeStatus::Success);
    S::StateComparison<Local>::Block=nullptr;
    assert(owner.Set(3)==S::StateSetStatus::NotRunning);
    S::StateSnapshot<Local> localRead{};assert(local.TryRead<Local>(localRead) && localRead.Value==2);
    assert(local.Start()!=S::StateRuntimeStatus::Success);

    Adapter adapter;
    S::StateTransportBinding<Remote,Serializable::DirectBinary> binding;
    assert((binding.Initialize<Adapter,&Adapter::Admit,&Adapter::Validate,&Adapter::Wake>(adapter)));
    S::Runtime<S::TypeConfiguration<Remote,S::MaximumSubscribers<1>,S::MaximumRemoteOwners<1>>> remote;
    auto remoteOwner=remote.BindOwner<Remote>();assert(remoteOwner);
    assert(remote.BindTransport(binding)==S::StateRuntimeStatus::Success);
    assert(remote.Initialize(directory.View(),&Capture)==S::StateRuntimeStatus::Success);
    assert(remote.Start()==S::StateRuntimeStatus::Success);
    const auto peer=Identity(2);
    assert(remote.ReserveSourceSubscriber<Remote>(peer,S::StateSessionToken{1})==S::StateRemoteStatus::Success);
    assert(remote.ActivateSourceSubscriber<Remote>(peer,S::StateSessionToken{1},true,{false,1})==S::StateRemoteStatus::Success);
    assert(remoteOwner.Set({1})==S::StateSetStatus::Changed);
    const auto session=remote.ReserveSubscriptionSession<Remote>(peer.Device);assert(session);
    assert(remote.InstallSubscribeSnapshot<Remote>(peer,session.Handle.Session,{false,1},{{9},Capture()})==S::StateRemoteStatus::Success);
    Gate transfer;adapter.Block=&transfer;
    auto serving=std::async(std::launch::async,[&]{return remote.ServiceLatest<Remote>();});
    transfer.WaitEntered();
    auto remoteStopped=std::async(std::launch::async,[&]{return remote.Shutdown();});
    WaitClosed(remote);
    assert(remoteStopped.wait_for(20ms)==std::future_status::timeout);
    assert(remoteOwner.Set({2})==S::StateSetStatus::NotRunning);
    assert(remote.ServiceLatest<Remote>().Status==S::StateTransportAdmissionStatus::Quiesced);
    transfer.Release();
    assert(serving.get());assert(remoteStopped.get()==S::StateRuntimeStatus::Success);
    assert(!S::StateTypeRuntime<Remote>::Get().HasTransport());
    assert(remote.GetSourceSubscriberStatus<Remote>(peer.Device)==S::StateRemoteSessionState::Inactive);
    assert(remote.GetRemoteSessionStatus<Remote>(peer.Device)==S::StateRemoteSessionState::Inactive);
    S::StateSnapshot<Remote> retained{};assert(remote.TryReadRemote<Remote>(peer.Device,retained) && retained.Value.Number==9);
    assert(remoteOwner.Set({2})==S::StateSetStatus::NotRunning);
}
