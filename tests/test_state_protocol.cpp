#include <array>
#include <cassert>

#include <ESPressio_StateProtocol.hpp>

using namespace ESPressio::State;

struct TestState {
    using Value = uint32_t;
    static constexpr StateTypeId Id = 0x1001;
};

template<>
struct ESPressio::State::StateCodec<TestState> {
    static bool Encode(const uint32_t& value, uint8_t* output, std::size_t capacity, std::size_t& size) {
        if (capacity < 4) return false;
        for (std::size_t index = 0; index < 4; ++index) output[index] = static_cast<uint8_t>((value >> (index * 8)) & 0xFFu);
        size = 4;
        return true;
    }
    static bool Decode(const uint8_t* input, std::size_t size, uint32_t& value) {
        if (input == nullptr || size != 4) return false;
        value = 0;
        for (std::size_t index = 0; index < 4; ++index) value |= static_cast<uint32_t>(input[index]) << (index * 8);
        return true;
    }
};

int main() {
    DeviceIdentifier::Storage bytes{};
    bytes[15] = 0x42;
    const DeviceIdentifier device(bytes);

    std::array<uint8_t, 128> buffer{};
    std::size_t size = 0;

    const StateProtocol::ControlMessage subscribeResult{StateProtocol::MessageType::SubscribeResult, device, TestState::Id};
    assert(StateProtocol::EncodeControl(subscribeResult, buffer.data(), buffer.size(), size));
    StateProtocol::ControlMessage decoded{};
    assert(StateProtocol::DecodeControl(buffer.data(), size, decoded));
    assert(decoded.Type == StateProtocol::MessageType::SubscribeResult);
    assert(decoded.Device == device);
    assert(decoded.TypeId == TestState::Id);

    StateUpdate<uint32_t> publication{};
    publication.Header.Origin = device;
    publication.Header.TypeId = TestState::Id;
    publication.Header.Epoch = 3;
    publication.Header.Revision = 42;
    publication.Value = 0x12345678u;
    assert(StateProtocol::EncodeUpdate<TestState>(publication, buffer.data(), buffer.size(), size));

    StateProtocol::ParsedUpdate parsed{};
    assert(StateProtocol::DecodeUpdate(buffer.data(), size, parsed));
    assert(parsed.Header.Origin == device);
    assert(parsed.Header.Epoch == 3);
    assert(parsed.Header.Revision == 42);
    uint32_t value = 0;
    assert(StateProtocol::DecodeValue<TestState>(parsed, value));
    assert(value == publication.Value);

    return 0;
}
