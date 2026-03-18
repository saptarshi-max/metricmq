// MetricMQ.h - ESP32/Arduino Client Library
// Lightweight binary protocol client for MetricMQ broker
// Supports Ed25519 message signing (using ESP32 mbedtls or libsodium)
//
// Wire format (16-byte header, matches desktop broker):
//   [Version:1][Command:1][Sequence:8][TopicLen:2][PayloadLen:4][Topic][Payload]
// Signed frames append:
//   [Signature:64][KeyID:4]

#ifndef METRICMQ_H
#define METRICMQ_H

#include <Arduino.h>
#include <WiFi.h>
#include <functional>

// Binary protocol version (must match desktop broker)
#define BINARY_PROTOCOL_VERSION 0x01

// Header size: version(1) + command(1) + sequence(8) + topic_len(2) + payload_len(4)
#define BINARY_HEADER_SIZE 16

// Binary protocol commands (matches src/binary_protocol.hpp exactly)
enum class BinaryCommand : uint8_t {
    SUBSCRIBE       = 0x01,
    UNSUBSCRIBE     = 0x02,
    PUBLISH         = 0x03,
    MESSAGE         = 0x04,
    ACK             = 0x05,
    PING            = 0x06,
    PONG            = 0x07,
    CMD_ERROR       = 0x08,
    SIGNED_PUBLISH  = 0x10,  // Client -> Broker: publish with Ed25519 signature
    SIGNED_MESSAGE  = 0x11   // Broker -> Client: deliver signed message
};

// Signature / key constants
#define METRICMQ_SIGNATURE_SIZE   64   // Ed25519 detached signature
#define METRICMQ_KEY_ID_SIZE      4    // uint32_t key identifier
#define METRICMQ_PUBLIC_KEY_SIZE  32
#define METRICMQ_SECRET_KEY_SIZE  64
#define METRICMQ_SEED_SIZE        32

// Connection states
enum class ConnectionState {
    DISCONNECTED,
    CONNECTING,
    CONNECTED,
    ERROR
};

// Standard callback: topic, payload, length, sequence
typedef std::function<void(const String& topic, const uint8_t* payload,
                           size_t length, uint64_t sequence)> SubscriptionCallback;

// Signed-message callback: adds is_signed, key_id, signature pointer
typedef std::function<void(const String& topic, const uint8_t* payload,
                           size_t length, uint64_t sequence,
                           bool is_signed, uint32_t key_id,
                           const uint8_t* signature)> SignedMessageCallback;

class MetricMQClient {
public:
    MetricMQClient();

    // ===== Initialization =====
    bool begin(const char* host, uint16_t port = 6379);
    bool begin(IPAddress ip, uint16_t port = 6379);

    // ===== Connection =====
    bool connect();
    bool connect(const char* clientId);
    void disconnect();
    bool isConnected();
    ConnectionState getState();

    // ===== Unsigned Messaging =====
    bool publish(const String& topic, const String& payload);
    bool publish(const String& topic, const uint8_t* payload, size_t length);
    bool subscribe(const String& topic);
    bool subscribe(const String& topic, SubscriptionCallback callback);
    bool unsubscribe(const String& topic);

    // ===== Ed25519 Signed Messaging =====

    /// Load a pre-generated Ed25519 keypair for signing outgoing messages.
    /// @param secret_key  64-byte Ed25519 secret key
    /// @param key_id      Key ID registered in the broker's TrustedKeyStore
    void setSigningKey(const uint8_t* secret_key, uint32_t key_id);

    /// Store a public key for optional local verification of incoming signed messages.
    /// @param public_key  32-byte Ed25519 public key
    /// @param key_id      Key ID to associate with this public key
    void addVerificationKey(const uint8_t* public_key, uint32_t key_id);

    /// Publish with Ed25519 signature. Requires setSigningKey() first.
    bool publishSigned(const String& topic, const String& payload);
    bool publishSigned(const String& topic, const uint8_t* payload, size_t length);

    /// Set a callback that receives signature metadata for signed messages.
    void setSignedCallback(SignedMessageCallback callback);

    /// Returns true if setSigningKey() has been called.
    bool isSigningEnabled() const;

    /// Enable/disable local verification of incoming SIGNED_MESSAGE frames.
    void enableLocalVerification(bool enable);

    // ===== Configuration =====
    void loop();
    void setClientId(const char* clientId);
    const char* getClientId() const;
    void setKeepAlive(uint32_t seconds);
    void setCallback(SubscriptionCallback callback);
    void enableExactlyOnce(bool enable);
    bool isExactlyOnceEnabled() const;

private:
    WiFiClient client_;
    String host_;
    IPAddress ip_;
    uint16_t port_;
    bool use_ip_;

    String client_id_;
    ConnectionState state_;
    SubscriptionCallback callback_;
    SignedMessageCallback signed_callback_;

    bool exactly_once_enabled_;
    uint64_t last_sequence_;

    uint32_t keep_alive_ms_;
    unsigned long last_activity_;
    unsigned long last_ping_;

    // Receive buffer (sized for header + topic + payload + sig + key_id)
    uint8_t recv_buffer_[2048];
    size_t recv_buffer_pos_;

    // === Signing state ===
    bool signing_enabled_;
    uint8_t secret_key_[METRICMQ_SECRET_KEY_SIZE];
    uint32_t signing_key_id_;

    // === Verification keys (for local verification of incoming signed msgs) ===
    bool local_verify_enabled_;
    static const int MAX_VERIFY_KEYS = 4;
    struct VerifyKey {
        uint8_t public_key[METRICMQ_PUBLIC_KEY_SIZE];
        uint32_t key_id;
        bool valid;
    };
    VerifyKey verify_keys_[MAX_VERIFY_KEYS];
    int verify_key_count_;

    // === Wire-format operations ===
    // Desktop-compatible format:
    //   [Version:1][Command:1][Sequence:8][TopicLen:2][PayloadLen:4][Topic][Payload]
    // Signed frames append: [Signature:64][KeyID:4]
    bool sendFrame(BinaryCommand cmd, const String& topic,
                   const uint8_t* payload = nullptr, size_t payload_len = 0,
                   uint64_t sequence = 0);
    bool sendSignedFrame(const String& topic, const uint8_t* payload, size_t payload_len,
                         const uint8_t* signature, uint32_t key_id, uint64_t sequence = 0);
    bool sendSubscribe(const String& topic);
    bool sendUnsubscribe(const String& topic);
    bool sendPublish(const String& topic, const uint8_t* payload, size_t length);
    bool sendAck(uint64_t sequence);
    bool sendPing();

    void handleIncomingData();
    bool parseFrame();
    void processMessage(const String& topic, const uint8_t* payload, size_t length,
                        uint64_t sequence, bool is_signed = false, uint32_t key_id = 0,
                        const uint8_t* signature = nullptr);

    // Ed25519 using ESP32's mbedtls (no external library needed)
    bool ed25519Sign(const uint8_t* message, size_t msg_len,
                     const uint8_t* secret_key, uint8_t* signature_out);
    bool ed25519Verify(const uint8_t* message, size_t msg_len,
                       const uint8_t* signature, const uint8_t* public_key);

    const VerifyKey* findVerifyKey(uint32_t key_id) const;

    void resetConnection();
    void updateLastActivity();
    bool checkConnection();
};

#endif // METRICMQ_H
