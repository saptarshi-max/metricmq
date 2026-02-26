// Binary Protocol Publisher and Subscriber
#pragma once
#include <string>
#include <functional>
#include <cstdint>
#include <array>

namespace metricmq {

class BinaryPublisher {
public:
    BinaryPublisher(const std::string& host = "127.0.0.1", int port = 6379);
    ~BinaryPublisher();

    // Publish unsigned message
    void send(const std::string& topic, const std::string& payload);

    // Publish with Ed25519 signature (requires setSigningKey first)
    void sendSigned(const std::string& topic, const std::string& payload);

    // Configure signing: secret_key is 64 bytes (libsodium format), key_id identifies the key in broker's keystore
    void setSigningKey(const std::array<uint8_t, 64>& secret_key, uint32_t key_id);

    bool isSigningEnabled() const { return signing_enabled_; }

private:
    int sock_ = -1;
    uint64_t sequence_ = 0;
    bool signing_enabled_ = false;
    std::array<uint8_t, 64> secret_key_{};
    uint32_t signing_key_id_ = 0;
};

// Callback for signed message reception
struct SignedMessageInfo {
    std::string topic;
    std::string payload;
    bool is_signed = false;
    uint32_t key_id = 0;
    std::array<uint8_t, 64> signature{};
    uint64_t sequence = 0;
};

class BinarySubscriber {
public:
    BinarySubscriber(const std::string& host = "127.0.0.1", int port = 6379);
    BinarySubscriber(const std::string& client_id, const std::string& host = "127.0.0.1", int port = 6379);
    ~BinarySubscriber();

    // Set client ID for exactly-once delivery (before subscribing)
    void setClientId(const std::string& client_id);

    // Blocking subscribe with callback (auto-ACK enabled by default)
    void subscribe(const std::string& topic,
                   std::function<void(const std::string& topic, const std::string& payload)> callback,
                   bool auto_ack = true);

    // Blocking subscribe with signed-message-aware callback
    void subscribeSigned(const std::string& topic,
                         std::function<void(const SignedMessageInfo& msg)> callback,
                         bool auto_ack = true);

    void run();  // blocking receive loop (for debugging)

private:
    void sendAck(uint64_t sequence);
    
    int sock_ = -1;
    uint64_t sequence_ = 0;
    std::string client_id_;
    bool auto_ack_ = true;
};

} // namespace metricmq
