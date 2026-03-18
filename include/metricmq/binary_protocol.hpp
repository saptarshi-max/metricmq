/**
 * @file binary_protocol.hpp
 * @brief Binary framing protocol for MetricMQ — compact, deterministic, ESP32-friendly.
 *
 * @details
 * Every frame begins with a fixed 16-byte header, followed by a variable-length topic
 * and payload. Signed frames append a 64-byte Ed25519 signature and a 4-byte key ID.
 *
 * @par Frame layout (bytes)
 * @code
 * [0]     Version   (uint8,  must equal BINARY_PROTOCOL_VERSION = 0x01)
 * [1]     Command   (uint8,  BinaryCommand enum)
 * [2..9]  Sequence  (uint64, big-endian, for exactly-once delivery)
 * [10..11] Topic Len (uint16, big-endian, max 256)
 * [12..15] Payload Len (uint32, big-endian, max 16 MB)
 * [16..16+T-1]         Topic   (UTF-8 string)
 * [16+T..16+T+P-1]     Payload (arbitrary bytes)
 * --- For CMD_SIGNED_PUBLISH / CMD_SIGNED_MESSAGE only ---
 * [16+T+P..16+T+P+63]  Signature (64-byte Ed25519)
 * [16+T+P+64..+67]     Key ID    (uint32, big-endian)
 * @endcode
 *
 * @par Message to sign
 * The signed region is `topic + payload` (raw bytes, no separator).
 * This is the canonical format used by the broker verifier and the ESP32 client.
 *
 * @see BinaryProtocol, BinaryFrame, BinaryCommand
 * @see include/metricmq/binary_pubsub.hpp for the client-side send/receive API
 */
#pragma once

#ifdef _WIN32
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
#include <array>

namespace metricmq {

/// Current binary protocol version. Frames with a different version byte are silently dropped.
constexpr uint8_t BINARY_PROTOCOL_VERSION = 1;

/**
 * @brief Command byte values carried in the binary frame header.
 *
 * Direction notation: **C→B** = client to broker, **B→C** = broker to client.
 */
enum class BinaryCommand : uint8_t {
    CMD_SUBSCRIBE      = 0x01,  ///< C→B: subscribe to a topic
    CMD_UNSUBSCRIBE    = 0x02,  ///< C→B: unsubscribe from a topic
    CMD_PUBLISH        = 0x03,  ///< C→B: publish an unsigned message
    CMD_MESSAGE        = 0x04,  ///< B→C: deliver a message to a subscriber
    CMD_ACK            = 0x05,  ///< C↔B: acknowledge receipt by sequence ID
    CMD_PING           = 0x06,  ///< C↔B: keepalive probe
    CMD_PONG           = 0x07,  ///< C↔B: keepalive response
    CMD_ERROR          = 0x08,  ///< B→C: error response (payload = UTF-8 reason string)
    CMD_SIGNED_PUBLISH = 0x10,  ///< C→B: publish with Ed25519 signature appended
    CMD_SIGNED_MESSAGE = 0x11   ///< B→C: deliver signed message (signature forwarded)
};

/// Size of an Ed25519 signature in bytes.
constexpr size_t SIGNATURE_SIZE = 64;
/// Size of the key identifier field in bytes.
constexpr size_t KEY_ID_SIZE = 4;

/**
 * @brief Decoded representation of a single binary protocol frame.
 *
 * Frames are produced by @ref BinaryProtocol::parse() and consumed by
 * @ref BinaryProtocol::serialize(). The factory static methods provide
 * convenient constructors for each command type.
 *
 * @note The `signature` and `key_id` fields are only meaningful when
 *       `is_signed == true` (i.e. command is CMD_SIGNED_PUBLISH or CMD_SIGNED_MESSAGE).
 */
struct BinaryFrame {
    uint8_t       version;   ///< Protocol version byte (always BINARY_PROTOCOL_VERSION after parse)
    BinaryCommand command;   ///< Command type
    uint64_t      sequence;  ///< Monotonically increasing message ID (for exactly-once delivery)
    std::string   topic;     ///< Topic string (max 256 bytes)
    std::string   payload;   ///< Message payload (max 16 MB)

    std::array<uint8_t, 64> signature{}; ///< Ed25519 signature (valid only when is_signed)
    uint32_t                key_id{0};   ///< Key ID for TrustedKeyStore lookup (valid only when is_signed)
    bool                    is_signed{false}; ///< True when frame carries a signature

    BinaryFrame() : version(BINARY_PROTOCOL_VERSION), command(BinaryCommand::CMD_PING), sequence(0) {}

    /// @name Frame factories
    /// @{

    /** @brief Create a SUBSCRIBE frame. */
    static BinaryFrame subscribe(const std::string& topic, uint64_t seq = 0) {
        BinaryFrame frame;
        frame.command = BinaryCommand::CMD_SUBSCRIBE;
        frame.topic = topic;
        frame.sequence = seq;
        return frame;
    }

    /** @brief Create an UNSUBSCRIBE frame. */
    static BinaryFrame unsubscribe(const std::string& topic, uint64_t seq = 0) {
        BinaryFrame frame;
        frame.command = BinaryCommand::CMD_UNSUBSCRIBE;
        frame.topic = topic;
        frame.sequence = seq;
        return frame;
    }

    /** @brief Create an unsigned PUBLISH frame. */
    static BinaryFrame publish(const std::string& topic, const std::string& payload, uint64_t seq = 0) {
        BinaryFrame frame;
        frame.command = BinaryCommand::CMD_PUBLISH;
        frame.topic = topic;
        frame.payload = payload;
        frame.sequence = seq;
        return frame;
    }

    /**
     * @brief Create a MESSAGE delivery frame (broker → client).
     * @note The broker generates this automatically; clients do not send it.
     */
    static BinaryFrame message(const std::string& topic, const std::string& payload, uint64_t seq = 0) {
        BinaryFrame frame;
        frame.command = BinaryCommand::CMD_MESSAGE;
        frame.topic = topic;
        frame.payload = payload;
        frame.sequence = seq;
        return frame;
    }

    /**
     * @brief Create an ACK frame for a given sequence ID.
     * @param seq The sequence number being acknowledged.
     */
    static BinaryFrame ack(uint64_t seq) {
        BinaryFrame frame;
        frame.command = BinaryCommand::CMD_ACK;
        frame.sequence = seq;
        return frame;
    }

    /** @brief Create a PING keepalive frame. */
    static BinaryFrame ping() {
        BinaryFrame frame;
        frame.command = BinaryCommand::CMD_PING;
        return frame;
    }

    /** @brief Create a PONG keepalive response. */
    static BinaryFrame pong() {
        BinaryFrame frame;
        frame.command = BinaryCommand::CMD_PONG;
        return frame;
    }

    /**
     * @brief Create an ERROR response frame.
     * @param message Human-readable UTF-8 error description.
     */
    static BinaryFrame error(const std::string& message) {
        BinaryFrame frame;
        frame.command = BinaryCommand::CMD_ERROR;
        frame.payload = message;
        return frame;
    }

    /**
     * @brief Create a CMD_SIGNED_PUBLISH frame (client → broker).
     *
     * @param topic   Topic to publish to (max 256 bytes).
     * @param payload Message body (max 16 MB).
     * @param sig     64-byte Ed25519 signature over `topic + payload`.
     * @param key_id  ID of the signing key registered in TrustedKeyStore.
     * @param seq     Optional sequence number (assigned by BinaryPublisher if 0).
     */
    static BinaryFrame signed_publish(const std::string& topic, const std::string& payload,
                                      const std::array<uint8_t, 64>& sig, uint32_t key_id,
                                      uint64_t seq = 0) {
        BinaryFrame frame;
        frame.command = BinaryCommand::CMD_SIGNED_PUBLISH;
        frame.topic = topic;
        frame.payload = payload;
        frame.signature = sig;
        frame.key_id = key_id;
        frame.is_signed = true;
        frame.sequence = seq;
        return frame;
    }

    /**
     * @brief Create a CMD_SIGNED_MESSAGE delivery frame (broker → subscriber).
     *
     * The broker forwards the original signature so subscribers can independently
     * verify message authenticity without trusting the broker.
     */
    static BinaryFrame signed_message(const std::string& topic, const std::string& payload,
                                      const std::array<uint8_t, 64>& sig, uint32_t key_id,
                                      uint64_t seq = 0) {
        BinaryFrame frame;
        frame.command = BinaryCommand::CMD_SIGNED_MESSAGE;
        frame.topic = topic;
        frame.payload = payload;
        frame.signature = sig;
        frame.key_id = key_id;
        frame.is_signed = true;
        frame.sequence = seq;
        return frame;
    }
    /// @}
};

/**
 * @brief Stateless binary protocol serializer and parser.
 *
 * All methods are static. The class holds no state — create frames with
 * @ref BinaryFrame factories, serialize them, send over TCP, receive bytes
 * into a buffer, and call @ref parse() when you have at least @ref MIN_FRAME_SIZE bytes.
 *
 * @par Typical usage (publisher side)
 * @code
 * auto frame = BinaryFrame::publish("sensors/temp", "22.5", ++seq);
 * std::string wire = BinaryProtocol::serialize(frame);
 * send(sock, wire.data(), wire.size(), 0);
 * @endcode
 *
 * @par Typical usage (subscriber side)
 * @code
 * recv_buffer.append(chunk, n);
 * while (auto result = BinaryProtocol::parse(recv_buffer)) {
 *     auto [frame, consumed] = *result;
 *     recv_buffer.erase(0, consumed);
 *     handle(frame);
 * }
 * @endcode
 */
class BinaryProtocol {
public:
    /**
     * @brief Serialize a frame to a wire-format byte string.
     * @param frame The frame to serialize.
     * @return Byte string ready to be written to a TCP socket.
     */
    static std::string serialize(const BinaryFrame& frame);

    /**
     * @brief Parse the first complete frame from a byte buffer.
     *
     * @param buffer The receive buffer (may contain multiple frames or a partial frame).
     * @return A pair of `{frame, bytes_consumed}` on success, or `std::nullopt` when:
     *   - The buffer contains fewer than @ref MIN_FRAME_SIZE bytes (incomplete header).
     *   - The header is complete but the payload has not yet arrived.
     *   - The version byte does not match @ref BINARY_PROTOCOL_VERSION.
     *   - `topic_len > MAX_TOPIC_LEN` or `payload_len > MAX_PAYLOAD_LEN` (DoS guard).
     *
     * @note This function never throws. All error paths return `std::nullopt`.
     * @note `bytes_consumed` is the number of bytes to remove from the front of `buffer`.
     */
    static std::optional<std::pair<BinaryFrame, size_t>> parse(const std::string& buffer);

    /// Minimum number of bytes that must be present before parse() can read the header.
    static constexpr size_t MIN_FRAME_SIZE = 16;

    /**
     * @brief Compute the serialized size of a frame given topic and payload lengths.
     * @note Does not account for the signature extension (add 68 for signed frames).
     */
    static size_t calculateFrameSize(const std::string& topic, const std::string& payload) {
        return MIN_FRAME_SIZE + topic.size() + payload.size();
    }

private:
    static void     writeUint16(std::string& buffer, uint16_t value);
    static void     writeUint32(std::string& buffer, uint32_t value);
    static void     writeUint64(std::string& buffer, uint64_t value);
    static uint16_t readUint16(const std::string& buffer, size_t offset);
    static uint32_t readUint32(const std::string& buffer, size_t offset);
    static uint64_t readUint64(const std::string& buffer, size_t offset);
};

} // namespace metricmq
