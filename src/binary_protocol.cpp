// MetricMQ Binary Protocol Implementation
#include "binary_protocol.hpp"
#include <cstring>
#include <stdexcept>

namespace metricmq {

// Big-endian write helpers
void BinaryProtocol::writeUint16(std::string& buffer, uint16_t value) {
    buffer.push_back(static_cast<char>((value >> 8) & 0xFF));
    buffer.push_back(static_cast<char>(value & 0xFF));
}

void BinaryProtocol::writeUint32(std::string& buffer, uint32_t value) {
    buffer.push_back(static_cast<char>((value >> 24) & 0xFF));
    buffer.push_back(static_cast<char>((value >> 16) & 0xFF));
    buffer.push_back(static_cast<char>((value >> 8) & 0xFF));
    buffer.push_back(static_cast<char>(value & 0xFF));
}

void BinaryProtocol::writeUint64(std::string& buffer, uint64_t value) {
    buffer.push_back(static_cast<char>((value >> 56) & 0xFF));
    buffer.push_back(static_cast<char>((value >> 48) & 0xFF));
    buffer.push_back(static_cast<char>((value >> 40) & 0xFF));
    buffer.push_back(static_cast<char>((value >> 32) & 0xFF));
    buffer.push_back(static_cast<char>((value >> 24) & 0xFF));
    buffer.push_back(static_cast<char>((value >> 16) & 0xFF));
    buffer.push_back(static_cast<char>((value >> 8) & 0xFF));
    buffer.push_back(static_cast<char>(value & 0xFF));
}

// Big-endian read helpers
uint16_t BinaryProtocol::readUint16(const std::string& buffer, size_t offset) {
    if (offset + 2 > buffer.size()) {
        throw std::runtime_error("Buffer underflow reading uint16");
    }
    return (static_cast<uint16_t>(static_cast<uint8_t>(buffer[offset])) << 8) |
           static_cast<uint16_t>(static_cast<uint8_t>(buffer[offset + 1]));
}

uint32_t BinaryProtocol::readUint32(const std::string& buffer, size_t offset) {
    if (offset + 4 > buffer.size()) {
        throw std::runtime_error("Buffer underflow reading uint32");
    }
    return (static_cast<uint32_t>(static_cast<uint8_t>(buffer[offset])) << 24) |
           (static_cast<uint32_t>(static_cast<uint8_t>(buffer[offset + 1])) << 16) |
           (static_cast<uint32_t>(static_cast<uint8_t>(buffer[offset + 2])) << 8) |
           static_cast<uint32_t>(static_cast<uint8_t>(buffer[offset + 3]));
}

uint64_t BinaryProtocol::readUint64(const std::string& buffer, size_t offset) {
    if (offset + 8 > buffer.size()) {
        throw std::runtime_error("Buffer underflow reading uint64");
    }
    return (static_cast<uint64_t>(static_cast<uint8_t>(buffer[offset])) << 56) |
           (static_cast<uint64_t>(static_cast<uint8_t>(buffer[offset + 1])) << 48) |
           (static_cast<uint64_t>(static_cast<uint8_t>(buffer[offset + 2])) << 40) |
           (static_cast<uint64_t>(static_cast<uint8_t>(buffer[offset + 3])) << 32) |
           (static_cast<uint64_t>(static_cast<uint8_t>(buffer[offset + 4])) << 24) |
           (static_cast<uint64_t>(static_cast<uint8_t>(buffer[offset + 5])) << 16) |
           (static_cast<uint64_t>(static_cast<uint8_t>(buffer[offset + 6])) << 8) |
           static_cast<uint64_t>(static_cast<uint8_t>(buffer[offset + 7]));
}

std::string BinaryProtocol::serialize(const BinaryFrame& frame) {
    // Calculate size: base + topic + payload + optional signature (64B) + key_id (4B)
    size_t extra_size = frame.is_signed ? (SIGNATURE_SIZE + KEY_ID_SIZE) : 0;
    std::string buffer;
    buffer.reserve(MIN_FRAME_SIZE + frame.topic.size() + frame.payload.size() + extra_size);

    // Header: [Version: 1B][Command: 1B][Sequence: 8B][Topic Len: 2B][Payload Len: 4B]
    buffer.push_back(static_cast<char>(frame.version));
    buffer.push_back(static_cast<char>(frame.command));
    writeUint64(buffer, frame.sequence);
    writeUint16(buffer, static_cast<uint16_t>(frame.topic.size()));
    writeUint32(buffer, static_cast<uint32_t>(frame.payload.size()));

    // Variable-length data
    buffer.append(frame.topic);
    buffer.append(frame.payload);

    // Append signature data for signed frames
    if (frame.is_signed) {
        buffer.append(reinterpret_cast<const char*>(frame.signature.data()), SIGNATURE_SIZE);
        writeUint32(buffer, frame.key_id);
    }

    return buffer;
}

std::optional<std::pair<BinaryFrame, size_t>> BinaryProtocol::parse(const std::string& buffer) {
    // Need at least header
    if (buffer.size() < MIN_FRAME_SIZE) {
        return std::nullopt;
    }

    BinaryFrame frame;
    size_t offset = 0;

    // Parse header
    frame.version = static_cast<uint8_t>(buffer[offset++]);
    
    // Version check
    if (frame.version != BINARY_PROTOCOL_VERSION) {
        throw std::runtime_error("Unsupported protocol version: " + std::to_string(frame.version));
    }

    frame.command = static_cast<BinaryCommand>(buffer[offset++]);
    frame.sequence = readUint64(buffer, offset);
    offset += 8;

    uint16_t topic_len = readUint16(buffer, offset);
    offset += 2;

    uint32_t payload_len = readUint32(buffer, offset);
    offset += 4;

    // Check if this is a signed frame
    bool is_signed_cmd = (frame.command == BinaryCommand::CMD_SIGNED_PUBLISH ||
                          frame.command == BinaryCommand::CMD_SIGNED_MESSAGE);
    size_t sig_size = is_signed_cmd ? (SIGNATURE_SIZE + KEY_ID_SIZE) : 0;

    // Check if we have complete frame
    size_t total_size = MIN_FRAME_SIZE + topic_len + payload_len + sig_size;
    if (buffer.size() < total_size) {
        return std::nullopt;  // Incomplete frame
    }

    // Parse variable-length data
    if (topic_len > 0) {
        frame.topic = buffer.substr(offset, topic_len);
        offset += topic_len;
    }

    if (payload_len > 0) {
        frame.payload = buffer.substr(offset, payload_len);
        offset += payload_len;
    }

    // Parse signature data for signed frames
    if (is_signed_cmd) {
        std::memcpy(frame.signature.data(), buffer.data() + offset, SIGNATURE_SIZE);
        offset += SIGNATURE_SIZE;
        frame.key_id = readUint32(buffer, offset);
        offset += KEY_ID_SIZE;
        frame.is_signed = true;
    }

    return std::make_pair(frame, total_size);
}

} // namespace metricmq
