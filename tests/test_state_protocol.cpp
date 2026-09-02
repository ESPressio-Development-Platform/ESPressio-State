#include <array>
#include <cassert>

#include <ESPressio_StateProtocol.hpp>

using namespace ESPressio::State;

int main() {
    DeviceIdentifier::Storage bytes{};
    bytes[15] = 0x42;
    const DeviceIdentifier device(bytes);

    std::array<uint8_t, StateProtocol::ControlSize> buffer{};
    std::size_t size = 0;

    const StateProtocol::ControlMessage subscribeAck{
        StateProtocol::MessageType::SubscribeAcknowledgement,
        device,
        0x1001
    };
    assert(StateProtocol::EncodeControl(subscribeAck, buffer.data(), buffer.size(), size));
    assert(size == StateProtocol::ControlSize);

    StateProtocol::ControlMessage decoded{};
    assert(StateProtocol::DecodeControl(buffer.data(), size, decoded));
    assert(decoded.Type == StateProtocol::MessageType::SubscribeAcknowledgement);
    assert(decoded.Device == device);
    assert(decoded.TypeId == 0x1001);

    const StateProtocol::ControlMessage unsubscribeAck{
        StateProtocol::MessageType::UnsubscribeAcknowledgement,
        device,
        0x1001
    };
    assert(StateProtocol::EncodeControl(unsubscribeAck, buffer.data(), buffer.size(), size));
    assert(StateProtocol::DecodeControl(buffer.data(), size, decoded));
    assert(decoded.Type == StateProtocol::MessageType::UnsubscribeAcknowledgement);
    assert(decoded.Device == device);
    assert(decoded.TypeId == 0x1001);

    return 0;
}
