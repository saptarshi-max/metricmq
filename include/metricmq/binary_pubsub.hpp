/**
 * @file binary_pubsub.hpp
 * @brief High-level binary-protocol publisher and subscriber clients.
 *
 * @details
 * `BinaryPublisher` and `BinarySubscriber` wrap the raw @ref BinaryProtocol framing
 * into convenient send/receive APIs. Both classes maintain a single persistent TCP
 * connection to the broker and handle sequence numbering internally.
 *
 * @par Exactly-once delivery
 * Pass a `client_id` to the `BinarySubscriber` constructor to enable exactly-once
 * semantics. The broker persists ACK state per `client_id` so a reconnecting client
 * only receives messages it has not yet acknowledged.
 *
 * @par Signed publish
 * Call `BinaryPublisher::setSigningKey()` once with the device's 64-byte Ed25519
 * secret key and the key ID registered in the broker's `TrustedKeyStore`, then use
 * `sendSigned()` instead of `send()`.
 */
#pragma once
#include <string>
#include <functional>
#include <cstdint>
#include <array>

namespace metricmq {

/**
 * @brief Binary-protocol message publisher.
 *
 * Opens one TCP connection on construction and keeps it alive for the lifetime
 * of the object. Thread safety: **not thread-safe** — use one instance per thread.
 *
 * @par Example
 * @code
 * metricmq::BinaryPublisher pub("192.168.1.100", 6379);
 * pub.send("sensors/temp", "22.5");
 * @endcode
 */
class BinaryPublisher {
public:
    /**
     * @brief Connect to a MetricMQ broker.
     * @param host Broker hostname or IP (default: `"127.0.0.1"`).
     * @param port Broker TCP port (default: 6379).
     * @throws std::runtime_error if the TCP connection fails.
     */
    BinaryPublisher(const std::string& host = "127.0.0.1", int port = 6379);

    /** @brief Close the TCP connection. */
    ~BinaryPublisher();

    /**
     * @brief Publish an unsigned message.
     * @param topic   Topic string (max 256 bytes).
     * @param payload Message body (max 16 MB).
     */
    void send(const std::string& topic, const std::string& payload);

    /**
     * @brief Publish a message with an Ed25519 signature.
     *
     * The broker verifies the signature against the public key registered under
     * `key_id`. If verification fails or the key is disabled, the broker returns
     * `CMD_ERROR` and does **not** deliver the message.
     *
     * @pre `setSigningKey()` must be called before this method.
     * @param topic   Topic string. Must match the `allowed_topics` scope of the key.
     * @param payload Message body.
     * @throws std::logic_error if signing has not been configured.
     */
    void sendSigned(const std::string& topic, const std::string& payload);

    /**
     * @brief Configure the Ed25519 signing key.
     *
     * @param secret_key 64-byte Ed25519 secret key (libsodium expanded format).
     *                   Call `crypto::secure_zero()` on this array after use.
     * @param key_id     Numeric identifier matching a key pre-registered in the
     *                   broker's `TrustedKeyStore`.
     */
    void setSigningKey(const std::array<uint8_t, 64>& secret_key, uint32_t key_id);

    /** @brief Returns `true` if a signing key has been set via `setSigningKey()`. */
    bool isSigningEnabled() const { return signing_enabled_; }

private:
    int      sock_ = -1;
    uint64_t sequence_ = 0;
    bool     signing_enabled_ = false;
    std::array<uint8_t, 64> secret_key_{};
    uint32_t signing_key_id_ = 0;
};

/**
 * @brief Metadata attached to a received signed message.
 *
 * Passed to the callback registered via `BinarySubscriber::subscribeSigned()`.
 * The `signature` field allows the subscriber to independently verify message
 * authenticity without trusting the broker (end-to-end verification).
 */
struct SignedMessageInfo {
    std::string topic;                  ///< Topic the message was published to.
    std::string payload;                ///< Message body.
    bool        is_signed = false;      ///< True when received as CMD_SIGNED_MESSAGE.
    uint32_t    key_id = 0;             ///< Key ID used by the publisher (0 if unsigned).
    std::array<uint8_t, 64> signature{}; ///< Raw Ed25519 signature bytes.
    uint64_t    sequence = 0;           ///< Broker-assigned sequence ID.
};

/**
 * @brief Binary-protocol message subscriber with optional exactly-once delivery.
 *
 * Call `subscribe()` or `subscribeSigned()` to register a topic and callback,
 * then let the call block (it runs the receive loop internally).
 *
 * @par Exactly-once delivery
 * @code
 * // Use the two-argument constructor to enable ACK tracking:
 * metricmq::BinarySubscriber sub("my-device-01", "broker-host", 6379);
 * sub.subscribe("sensors/temp", [](const std::string& t, const std::string& p) {
 *     process(p);
 * });
 * // Blocks here. On reconnect with the same client_id, only unACK'd messages replay.
 * @endcode
 *
 * @note `subscribe()` is blocking. Run it on a dedicated thread.
 */
class BinarySubscriber {
public:
    /**
     * @brief Anonymous subscriber (no exactly-once tracking).
     * @param host Broker hostname or IP (default: `"127.0.0.1"`).
     * @param port Broker TCP port (default: 6379).
     */
    BinarySubscriber(const std::string& host = "127.0.0.1", int port = 6379);

    /**
     * @brief Identified subscriber with exactly-once delivery guarantees.
     * @param client_id Unique device or client identifier. The broker uses this
     *                  to persist and replay unacknowledged messages across reconnects.
     * @param host Broker hostname or IP.
     * @param port Broker TCP port.
     */
    BinarySubscriber(const std::string& client_id, const std::string& host = "127.0.0.1", int port = 6379);

    /** @brief Close the TCP connection. */
    ~BinarySubscriber();

    /**
     * @brief Set or override the client identifier after construction.
     * @note Must be called before `subscribe()`.
     */
    void setClientId(const std::string& client_id);

    /**
     * @brief Subscribe to a topic and block until the connection closes.
     *
     * @param topic    Topic to subscribe to. Use `"#"` for a wildcard that receives
     *                 every message regardless of topic.
     * @param callback Invoked for each received message. Called on the current thread.
     * @param auto_ack When `true` (default), sends a CMD_ACK after each callback returns.
     *                 Set to `false` to handle ACKs manually.
     */
    void subscribe(const std::string& topic,
                   std::function<void(const std::string& topic, const std::string& payload)> callback,
                   bool auto_ack = true);

    /**
     * @brief Subscribe to a topic with access to signature metadata.
     *
     * Use this variant when your application needs to verify the Ed25519 signature
     * independently, or when it needs to know the `key_id` of the sender.
     *
     * @param topic    Topic to subscribe to.
     * @param callback Invoked with a @ref SignedMessageInfo for each message.
     * @param auto_ack When `true`, sends CMD_ACK after each callback returns.
     */
    void subscribeSigned(const std::string& topic,
                         std::function<void(const SignedMessageInfo& msg)> callback,
                         bool auto_ack = true);

    /**
     * @brief Run a raw receive loop (prints raw frames, for debugging).
     * @note Intended for development only. Prefer `subscribe()` in production code.
     */
    void run();

private:
    void sendAck(uint64_t sequence);

    int         sock_ = -1;
    uint64_t    sequence_ = 0;
    std::string client_id_;
    bool        auto_ack_ = true;
};

} // namespace metricmq
