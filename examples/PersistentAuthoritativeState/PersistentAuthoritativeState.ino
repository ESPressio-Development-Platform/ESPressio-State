#include <ESPressio_States.hpp>
#include <ESPressio_RuntimeIdentity.hpp>
#include <ESPressio_SerializationMacros.hpp>
#include <ESPressio_TypeDirectory.hpp>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <string_view>

using namespace ESPressio;
namespace S=ESPressio::State;

struct DurableValue final {
    std::uint32_t Reading=0;
    constexpr bool operator==(const DurableValue& other) const noexcept { return Reading==other.Reading; }
    ESPRESSIO_SERIALIZABLE_TYPE(DurableValue)
    ESPRESSIO_SERIALIZABLE_SCHEMA_VERSION(1)
    ESPRESSIO_SERIALIZABLE_PROPERTIES(ESPRESSIO_PROPERTY("reading",Reading))
};
struct DurableState final:S::SerializableState<DurableState,DurableValue> {
    static constexpr S::StateTypeId TypeId{0x1701};
    static constexpr std::string_view CanonicalName="Example.State.Persistent";
};

// Demonstration-only bounded atomic store. Production applications bind a real
// P4 IAtomicRecordStore that provides the same old-or-new replacement contract.
class MemoryStore final:public Persistence::IAtomicRecordStore {
    std::array<std::uint8_t,256> bytes_{};
    std::size_t size_=0;
    Persistence::AtomicRecordKey key_{};
    bool present_=false;
public:
    Persistence::AtomicRecordCapabilities Capabilities() const noexcept override {
        return {true,true,bytes_.size(),4};
    }
    Persistence::AtomicRecordStatus Recover() noexcept override {
        return Persistence::AtomicRecordStatus::Success;
    }
    Persistence::AtomicRecordStatus Read(const Persistence::AtomicRecordKey& key,std::uint8_t* output,
                                         std::size_t capacity,std::size_t& bytesRead) noexcept override {
        bytesRead=0;
        if(!present_ || !(key==key_)) return Persistence::AtomicRecordStatus::NotFound;
        if(capacity<size_) return Persistence::AtomicRecordStatus::BufferTooSmall;
        std::memcpy(output,bytes_.data(),size_);bytesRead=size_;
        return Persistence::AtomicRecordStatus::Success;
    }
    Persistence::AtomicRecordStatus ReplaceAtomically(const Persistence::AtomicRecordKey& key,
                                                       const std::uint8_t* data,std::size_t size) noexcept override {
        if(!data || size>bytes_.size()) return Persistence::AtomicRecordStatus::NoSpace;
        key_=key;std::memcpy(bytes_.data(),data,size);size_=size;present_=true;
        return Persistence::AtomicRecordStatus::Success;
    }
    Persistence::AtomicRecordStatus RemoveAfterCommit(const Persistence::AtomicRecordKey& key) noexcept override {
        if(present_ && key==key_){present_=false;size_=0;}
        return Persistence::AtomicRecordStatus::Success;
    }
};

static System::DeviceRuntimeIdentity Identity() {
    System::DeviceIdentifier::Storage bytes{};bytes[0]=0x17;
    return {System::DeviceIdentifier{bytes},System::RuntimeIncarnationId{1}};
}

MemoryStore store;
S::StatePersistenceBinding<DurableState,Serializable::DirectBinary> persistence(store);
Primitive::TypeDirectory<1> directory;
S::Runtime<S::TypeConfiguration<DurableState>> states;
S::StateOwner<DurableState> owner;

void setup() {
    assert(System::RuntimeIdentity::Install(Identity())==System::RuntimeIdentity::InstallationStatus::Success);
    (void)directory.Register<DurableState>();
    (void)directory.Initialize();
    owner=states.BindOwner<DurableState>();
    assert(states.BindPersistence<DurableState>(persistence)==S::StateRuntimeStatus::Success);
    assert(states.Initialize(directory.View())==S::StateRuntimeStatus::Success);
    assert(states.Start()==S::StateRuntimeStatus::Success);

    // ReplaceAtomically succeeds before RAM/version publication.
    assert(owner.Set({123},{1000,Timing::TimeReliability::Synchronized})==S::StateSetStatus::Changed);
}

void loop() {}
