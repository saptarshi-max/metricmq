// MetricMQ Binary Protocol - Lightweight framing for embedded systems
// Frame format (16-byte header):
// [Version: 1B][Command: 1B][Sequence: 8B][Topic Len: 2B][Payload Len: 4B][Topic][Payload]
#pragma once

#ifdef _WIN32
// Prevent Windows.h from defining ERROR macro
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#endif

#include <string>
#include <cstdint>
#include <optional>
#include <vector>

namespace metricmq {

// Protocol version
constexpr uint8_t BINARY_PROTOCOL_VERSION = 1;

// Command types
enum class BinaryCommand : uint8_t {
    CMD_SUBSCRIBE   = 0x01,  // Client -> Broker: subscribe to topic
    CMD_UNSUBSCRIBE = 0x02,  // Client -> Broker: unsubscribe from topic
    CMD_PUBLISH     = 0x03,  // Client -> Broker: publish message
    CMD_MESSAGE     = 0x04,  // Broker -> Client: deliver message
    CMD_ACK         = 0x05,  // Client/Broker: acknowledge receipt
    CMD_PING        = 0x06,  // Keepalive
    CMD_PONG        = 0x07,  // Keepalive response
    CMD_ERROR       = 0x08   // Error response
};

// Binary frame structure
struct BinaryFrame {
    uint8_t version;
    BinaryCommand command;
    uint64_t sequence;      // For exactly-once delivery
    std::string topic;
    std::string payload;

    BinaryFrame() : version(BINARY_PROTOCOL_VERSION), command(BinaryCommand::CMD_PING), sequence(0) {}
    
    static BinaryFrame subscribe(const std::string& topic, uint64_t seq = 0) {
        BinaryFrame frame;
        frame.command = BinaryCommand::CMD_SUBSCRIBE;
        frame.topic = topic;
        frame.sequence = seq;
        return frame;
    }

    static BinaryFrame unsubscribe(const std::string& topic, uint64_t seq = 0) {
        BinaryFrame frame;
        frame.command = BinaryCommand::CMD_UNSUBSCRIBE;
        frame.topic = topic;
        frame.sequence = seq;
        return frame;
    }

    static BinaryFrame publish(const std::string& topic, const std::string& payload, uint64_t seq = 0) {
        BinaryFrame frame;
        frame.command = BinaryCommand::CMD_PUBLISH;
        frame.topic = topic;
        frame.payload = payload;
        frame.sequence = seq;
        return frame;
    }

    static BinaryFrame message(const std::string& topic, const std::string& payload, uint64_t seq = 0) {
        BinaryFrame frame;
        frame.command = BinaryCommand::CMD_MESSAGE;
        frame.topic = topic;
        frame.payload = payload;
        frame.sequence = seq;
        return frame;
    }

    static BinaryFrame ack(uint64_t seq) {
        BinaryFrame frame;
        frame.command = BinaryCommand::CMD_ACK;
        frame.sequence = seq;
        return frame;
    }

    static BinaryFrame ping() {
        BinaryFrame frame;
        frame.command = BinaryCommand::CMD_PING;
        return frame;
    }

    static BinaryFrame pong() {
        BinaryFrame frame;
        frame.command = BinaryCommand::CMD_PONG;
        return frame;
    }

    static BinaryFrame error(const std::string& message) {
        BinaryFrame frame;
        frame.command = BinaryCommand::CMD_ERROR;
        frame.payload = message;
        return frame;
    }
};

class BinaryProtocol {
public:
    // Serialize frame to binary buffer
    static std::string serialize(const BinaryFrame& frame);

    // Parse frame from buffer
    // Returns frame and bytes consumed, or nullopt if incomplete
    static std::optional<std::pair<BinaryFrame, size_t>> parse(const std::string& buffer);

    // Get minimum frame size (header only)
    static constexpr size_t MIN_FRAME_SIZE = 16;

    // Calculate total frame size for a given topic/payload
    static size_t calculateFrameSize(const std::string& topic, const std::string& payload) {
        return MIN_FRAME_SIZE + topic.size() + payload.size();
    }

private:
    // Helper: write uint16_t as big-endian
    static void writeUint16(std::string& buffer, uint16_t value);
    
    // Helper: write uint32_t as big-endian
    static void writeUint32(std::string& buffer, uint32_t value);
    
    // Helper: write uint64_t as big-endian
    static void writeUint64(std::string& buffer, uint64_t value);
    
    // Helper: read uint16_t as big-endian
    static uint16_t readUint16(const std::string& buffer, size_t offset);
    
    // Helper: read uint32_t as big-endian
    static uint32_t readUint32(const std::string& buffer, size_t offset);
    
    // Helper: read uint64_t as big-endian
    static uint64_t readUint64(const std::string& buffer, size_t offset);
};

} // namespace metricmq
