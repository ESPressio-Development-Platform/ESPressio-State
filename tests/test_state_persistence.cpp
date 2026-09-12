#include <ESPressio_States.hpp>
#include <ESPressio_SerializationMacros.hpp>
#include <ESPressio_TypeDirectory.hpp>
#include <array>
#include <cassert>
#include <cstring>
#include <string_view>
using namespace ESPressio;
namespace S=ESPressio::State;

struct PersistValue final {
    int Reading=0;
    constexpr bool operator==(const PersistValue& other) const noexcept { return Reading==other.Reading; }
    ESPRESSIO_SERIALIZABLE_TYPE(PersistValue)
    ESPRESSIO_SERIALIZABLE_SCHEMA_VERSION(1)
    ESPRESSIO_SERIALIZABLE_PROPERTIES(ESPRESSIO_PROPERTY("reading",Reading))
};
struct PersistentState final : S::SerializableState<PersistentState,PersistValue> {
    static constexpr S::StateTypeId TypeId{0x5101};
    static constexpr std::string_view CanonicalName="Test.State.Persistent";
};
struct CorruptState final : S::SerializableState<CorruptState,PersistValue> {
    static constexpr S::StateTypeId TypeId{0x5102};
    static constexpr std::string_view CanonicalName="Test.State.Corrupt";
};

class MemoryStore final : public Persistence::IAtomicRecordStore {
public:
    std::array<std::uint8_t,512> Bytes{};
    std::size_t Size=0;
    Persistence::AtomicRecordKey Key{};
    bool Present=false;
    bool FailReplace=false;
    unsigned ReplaceCalls=0;
    unsigned RecoverCalls=0;
    Persistence::AtomicRecordCapabilities Capabilities() const noexcept override { return {true,true,Bytes.size(),4}; }
    Persistence::AtomicRecordStatus Recover() noexcept override { ++RecoverCalls;return Persistence::AtomicRecordStatus::Success; }
    Persistence::AtomicRecordStatus Read(const Persistence::AtomicRecordKey& key,std::uint8_t* buffer,std::size_t capacity,std::size_t& bytesRead) noexcept override {
        bytesRead=0;if(!Present || !(key==Key)) return Persistence::AtomicRecordStatus::NotFound;
        if(capacity<Size) return Persistence::AtomicRecordStatus::BufferTooSmall;
        std::memcpy(buffer,Bytes.data(),Size);bytesRead=Size;return Persistence::AtomicRecordStatus::Success;
    }
    Persistence::AtomicRecordStatus ReplaceAtomically(const Persistence::AtomicRecordKey& key,const std::uint8_t* data,std::size_t size) noexcept override {
        ++ReplaceCalls;if(FailReplace) return Persistence::AtomicRecordStatus::StorageFailure;
        if(!data || size>Bytes.size()) return Persistence::AtomicRecordStatus::NoSpace;
        Key=key;std::memcpy(Bytes.data(),data,size);Size=size;Present=true;return Persistence::AtomicRecordStatus::Success;
    }
    Persistence::AtomicRecordStatus RemoveAfterCommit(const Persistence::AtomicRecordKey& key) noexcept override {
        if(Present && key==Key){Present=false;Size=0;}return Persistence::AtomicRecordStatus::Success;
    }
};

static Timing::QualifiedTime Captured(){ return {999,Timing::TimeReliability::Acquiring}; }
static System::DeviceRuntimeIdentity Identity(){
    System::DeviceIdentifier::Storage bytes{};bytes[0]=0x51;return {System::DeviceIdentifier{bytes},System::RuntimeIncarnationId{12}};
}

int main(){
    assert(System::RuntimeIdentity::Install(Identity())==System::RuntimeIdentity::InstallationStatus::Success);
    MemoryStore store;
    S::StatePersistenceBinding<PersistentState,Serializable::DirectBinary> persistence(store);
    auto raw=persistence.View();
    assert(raw && raw.Validate(raw.Owner));
    PersistValue old{41};
    const Timing::QualifiedTime original{123456,Timing::TimeReliability::Holdover};
    assert(raw.Commit(raw.Owner,old,original));
    assert(store.ReplaceCalls==1);

    Primitive::TypeDirectory<1> directory;
    assert(directory.Register<PersistentState>()==Primitive::TypeDirectoryRegistrationStatus::Success);
    assert(directory.Initialize()==Primitive::TypeDirectoryInitializationStatus::Success);
    using Runtime=S::Runtime<S::TypeConfiguration<PersistentState>>;
    Runtime runtime;
    auto owner=runtime.BindOwner<PersistentState>();assert(owner);
    assert(runtime.BindPersistence<PersistentState>(persistence)==S::StateRuntimeStatus::Success);
    assert(runtime.Initialize(directory.View(),&Captured)==S::StateRuntimeStatus::Success);
    S::StateSnapshot<PersistentState> snapshot{};
    assert(runtime.TryRead(snapshot));
    assert(snapshot.Value.Reading==41);
    assert(snapshot.TruthTime.Nanoseconds==original.Nanoseconds && snapshot.TruthTime.Reliability==original.Reliability);
    assert((runtime.Version<PersistentState>()==S::StateVersion{false,1}));
    assert(runtime.Start()==S::StateRuntimeStatus::Success);

    // Equal State is a strict no-op: no persistence write, no TruthTime refresh and no version burn.
    assert(owner.Set(PersistValue{41},{777,Timing::TimeReliability::Synchronized})==S::StateSetStatus::NoChange);
    assert(store.ReplaceCalls==1);
    assert(runtime.TryRead(snapshot) && snapshot.TruthTime.Nanoseconds==original.Nanoseconds);
    assert((runtime.Version<PersistentState>()==S::StateVersion{false,1}));

    // Durable failure occurs before RAM/version publication.
    store.FailReplace=true;
    assert(owner.Set(PersistValue{42},{888,Timing::TimeReliability::Synchronized})==S::StateSetStatus::PersistenceFailed);
    assert(store.ReplaceCalls==2);
    assert(runtime.TryRead(snapshot) && snapshot.Value.Reading==41 && snapshot.TruthTime.Nanoseconds==original.Nanoseconds);
    assert((runtime.Version<PersistentState>()==S::StateVersion{false,1}));

    store.FailReplace=false;
    assert(owner.Set(PersistValue{42},{888,Timing::TimeReliability::Synchronized})==S::StateSetStatus::Changed);
    assert(store.ReplaceCalls==3);
    assert(runtime.TryRead(snapshot) && snapshot.Value.Reading==42 && snapshot.TruthTime.Nanoseconds==888);
    assert((runtime.Version<PersistentState>()==S::StateVersion{false,2}));
    assert(runtime.Shutdown()==S::StateRuntimeStatus::Success);

    // Corrupt/incompatible committed bytes fail initialization closed rather than fabricating empty State.
    MemoryStore corrupt;
    S::StatePersistenceBinding<CorruptState,Serializable::DirectBinary> corruptBinding(corrupt);
    auto corruptView=corruptBinding.View();
    PersistValue seed{7};assert(corruptView.Commit(corruptView.Owner,seed,{7,Timing::TimeReliability::Synchronized}));
    corrupt.Bytes[0]^=0xff;
    Primitive::TypeDirectory<1> corruptDirectory;
    assert(corruptDirectory.Register<CorruptState>()==Primitive::TypeDirectoryRegistrationStatus::Success);
    assert(corruptDirectory.Initialize()==Primitive::TypeDirectoryInitializationStatus::Success);
    S::Runtime<S::TypeConfiguration<CorruptState>> corruptRuntime;
    auto corruptOwner=corruptRuntime.BindOwner<CorruptState>();assert(corruptOwner);
    assert(corruptRuntime.BindPersistence<CorruptState>(corruptBinding)==S::StateRuntimeStatus::Success);
    assert(corruptRuntime.Initialize(corruptDirectory.View(),&Captured)==S::StateRuntimeStatus::PersistenceFailure);
    S::StateSnapshot<CorruptState> absent{};assert(!corruptRuntime.TryRead(absent));
}
