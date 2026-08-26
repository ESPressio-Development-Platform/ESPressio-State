#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "ESPressio_DeviceIdentifier.hpp"
#include "ESPressio_StateCodec.hpp"
#include "ESPressio_StateTransport.hpp"

namespace ESPressio {
namespace State {

class StateProtocol final {
public:
    static constexpr uint16_t Magic = 0x5354; // "ST"
    static constexpr uint8_t Version = 1;

    enum class MessageType : uint8_t {
        Update = 1,
        Acknowledgement = 2,
        Subscribe = 3,
        Unsubscribe = 4,
        Resynchronize = 5,
        Disconnect = 6,
        SubscribeAcknowledgement = 7,
        UnsubscribeAcknowledgement = 8
    };

    struct ControlMessage {
        MessageType Type = MessageType::Subscribe;
        DeviceIdentifier Device{};
        StateTypeId TypeId = 0;
    };

    struct ParsedUpdate {
        StateUpdateHeader Header{};
        const uint8_t* Payload = nullptr;
        std::size_t PayloadSize = 0;
    };

    static constexpr std::size_t CommonHeaderSize = 4;
    static constexpr std::size_t UpdateHeaderSize = 42;
    static constexpr std::size_t AcknowledgementSize = 40;
    static constexpr std::size_t ControlSize = 28;

private:
    static void Write16(uint8_t* output, uint16_t value) {
        output[0] = static_cast<uint8_t>(value & 0xFFu);
        output[1] = static_cast<uint8_t>((value >> 8) & 0xFFu);
    }

    static void Write32(uint8_t* output, uint32_t value) {
        for (std::size_t index = 0; index < 4; ++index) {
            output[index] = static_cast<uint8_t>((value >> (index * 8)) & 0xFFu);
        }
    }

    static void Write64(uint8_t* output, uint64_t value) {
        for (std::size_t index = 0; index < 8; ++index) {
            output[index] = static_cast<uint8_t>((value >> (index * 8)) & 0xFFu);
        }
    }

    static uint16_t Read16(const uint8_t* input) {
        return static_cast<uint16_t>(input[0]) |
            (static_cast<uint16_t>(input[1]) << 8);
    }

    static uint32_t Read32(const uint8_t* input) {
        uint32_t value = 0;
        for (std::size_t index = 0; index < 4; ++index) {
            value |= static_cast<uint32_t>(input[index]) << (index * 8);
        }
        return value;
    }

    static uint64_t Read64(const uint8_t* input) {
        uint64_t value = 0;
        for (std::size_t index = 0; index < 8; ++index) {
            value |= static_cast<uint64_t>(input[index]) << (index * 8);
        }
        return value;
    }

    static bool WriteCommon(
        MessageType type,
        uint8_t* output,
        std::size_t capacity
    ) {
        if (output == nullptr || capacity < CommonHeaderSize) return false;
        Write16(output, Magic);
        output[2] = Version;
        output[3] = static_cast<uint8_t>(type);
        return true;
    }

    static bool ReadCommon(
        const uint8_t* input,
        std::size_t size,
        MessageType& type
    ) {
        if (input == nullptr || size < CommonHeaderSize) return false;
        if (Read16(input) != Magic || input[2] != Version) return false;
        type = static_cast<MessageType>(input[3]);
        return true;
    }

    static bool IsControlMessageType(MessageType type) {
        return
            type == MessageType::Subscribe ||
            type == MessageType::Unsubscribe ||
            type == MessageType::Resynchronize ||
            type == MessageType::Disconnect ||
            type == MessageType::SubscribeAcknowledgement ||
            type == MessageType::UnsubscribeAcknowledgement;
    }

public:
    static bool GetMessageType(
        const uint8_t* input,
        std::size_t size,
        MessageType& type
    ) {
        return ReadCommon(input, size, type);
    }

    template<typename TDefinition>
    static bool EncodeUpdate(
        const StateUpdate<StateValueType<TDefinition>>& update,
        uint8_t* output,
        std::size_t capacity,
        std::size_t& size
    ) {
        if (capacity < UpdateHeaderSize) return false;
        std::size_t payloadSize = 0;
        if (!StateCodec<TDefinition>::Encode(
                update.Value,
                output + UpdateHeaderSize,
                capacity - UpdateHeaderSize,
                payloadSize)) return false;
        if (payloadSize > 0xFFFFu) return false;
        if (!WriteCommon(MessageType::Update, output, capacity)) return false;
        std::memcpy(output + 4, update.Header.Origin.Bytes().data(), DeviceIdentifier::Size);
        Write64(output + 20, update.Header.TypeId);
        Write32(output + 28, update.Header.Epoch);
        Write64(output + 32, update.Header.Revision);
        Write16(output + 40, static_cast<uint16_t>(payloadSize));
        size = UpdateHeaderSize + payloadSize;
        return true;
    }

    static bool DecodeUpdate(
        const uint8_t* input,
        std::size_t size,
        ParsedUpdate& update
    ) {
        MessageType type;
        if (!ReadCommon(input, size, type) || type != MessageType::Update || size < UpdateHeaderSize) return false;
        const std::size_t payloadSize = Read16(input + 40);
        if (UpdateHeaderSize + payloadSize != size) return false;
        DeviceIdentifier::Storage origin{};
        std::memcpy(origin.data(), input + 4, origin.size());
        update.Header.Origin = DeviceIdentifier(origin);
        update.Header.TypeId = Read64(input + 20);
        update.Header.Epoch = Read32(input + 28);
        update.Header.Revision = Read64(input + 32);
        update.Payload = input + UpdateHeaderSize;
        update.PayloadSize = payloadSize;
        return update.Header.TypeId != 0 && update.Header.Revision != 0;
    }

    template<typename TDefinition>
    static bool DecodeValue(
        const ParsedUpdate& update,
        StateValueType<TDefinition>& value
    ) {
        if (update.Header.TypeId != StateTypeIdOf<TDefinition>) return false;
        return StateCodec<TDefinition>::Decode(update.Payload, update.PayloadSize, value);
    }

    static bool EncodeAcknowledgement(
        const StateAcknowledgement& acknowledgement,
        uint8_t* output,
        std::size_t capacity,
        std::size_t& size
    ) {
        if (capacity < AcknowledgementSize || !WriteCommon(MessageType::Acknowledgement, output, capacity)) return false;
        std::memcpy(output + 4, acknowledgement.Origin.Bytes().data(), DeviceIdentifier::Size);
        Write64(output + 20, acknowledgement.TypeId);
        Write32(output + 28, acknowledgement.Epoch);
        Write64(output + 32, acknowledgement.Revision);
        size = AcknowledgementSize;
        return true;
    }

    static bool DecodeAcknowledgement(
        const uint8_t* input,
        std::size_t size,
        StateAcknowledgement& acknowledgement
    ) {
        MessageType type;
        if (!ReadCommon(input, size, type) || type != MessageType::Acknowledgement || size != AcknowledgementSize) return false;
        DeviceIdentifier::Storage origin{};
        std::memcpy(origin.data(), input + 4, origin.size());
        acknowledgement.Origin = DeviceIdentifier(origin);
        acknowledgement.TypeId = Read64(input + 20);
        acknowledgement.Epoch = Read32(input + 28);
        acknowledgement.Revision = Read64(input + 32);
        return acknowledgement.TypeId != 0 && acknowledgement.Revision != 0;
    }

    static bool EncodeControl(
        const ControlMessage& control,
        uint8_t* output,
        std::size_t capacity,
        std::size_t& size
    ) {
        if (!IsControlMessageType(control.Type)) return false;
        if (capacity < ControlSize || !WriteCommon(control.Type, output, capacity)) return false;
        std::memcpy(output + 4, control.Device.Bytes().data(), DeviceIdentifier::Size);
        Write64(output + 20, control.TypeId);
        size = ControlSize;
        return true;
    }

    static bool DecodeControl(
        const uint8_t* input,
        std::size_t size,
        ControlMessage& control
    ) {
        MessageType type;
        if (!ReadCommon(input, size, type) || size != ControlSize) return false;
        if (!IsControlMessageType(type)) return false;
        DeviceIdentifier::Storage device{};
        std::memcpy(device.data(), input + 4, device.size());
        control.Type = type;
        control.Device = DeviceIdentifier(device);
        control.TypeId = Read64(input + 20);
        return true;
    }
};

}
}
