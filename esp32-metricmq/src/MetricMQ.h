// MetricMQ.h - ESP32/Arduino Client Library
// Lightweight binary protocol client for MetricMQ broker

#ifndef METRICMQ_H
#define METRICMQ_H

#include <Arduino.h>
#include <WiFi.h>
#include <functional>

// Binary protocol version
#define BINARY_PROTOCOL_VERSION 0x01

// Binary protocol commands
enum class BinaryCommand : uint8_t {
    SUBSCRIBE = 0x01,
    UNSUBSCRIBE = 0x02,
    PUBLISH = 0x03,
    MESSAGE = 0x04,
    PING = 0x05,
    ACK = 0x06
};

// Connection states
enum class ConnectionState {
    DISCONNECTED,
    CONNECTING,
    CONNECTED,
    ERROR
};

// Callback function type for received messages
typedef std::function<void(const String& topic, const uint8_t* payload, size_t length, uint64_t sequence)> SubscriptionCallback;

class MetricMQClient {
public:
    // Constructor
    MetricMQClient();
    
    // Initialize with broker address and port
    bool begin(const char* host, uint16_t port = 6379);
    bool begin(IPAddress ip, uint16_t port = 6379);
    
    // Connection management
    bool connect();
    bool connect(const char* clientId);
    void disconnect();
    bool isConnected();
    ConnectionState getState();
    
    // Messaging operations
    bool publish(const String& topic, const String& payload);
    bool publish(const String& topic, const uint8_t* payload, size_t length);
    bool subscribe(const String& topic);
    bool subscribe(const String& topic, SubscriptionCallback callback);
    bool unsubscribe(const String& topic);
    
    // Must be called regularly in loop()
    void loop();
    
    // Configuration
    void setClientId(const char* clientId);
    const char* getClientId() const;
    void setKeepAlive(uint32_t seconds);
    void setCallback(SubscriptionCallback callback);
    
    // Enable/disable exactly-once semantics (ACK)
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
    
    bool exactly_once_enabled_;
    uint64_t last_sequence_;
    
    uint32_t keep_alive_ms_;
    unsigned long last_activity_;
    unsigned long last_ping_;
    
    uint8_t recv_buffer_[2048];
    size_t recv_buffer_pos_;
    
    // Protocol operations
    bool sendFrame(BinaryCommand cmd, const String& topic, const uint8_t* payload = nullptr, size_t payload_len = 0, uint64_t sequence = 0);
    bool sendSubscribe(const String& topic);
    bool sendUnsubscribe(const String& topic);
    bool sendPublish(const String& topic, const uint8_t* payload, size_t length);
    bool sendAck(uint64_t sequence);
    bool sendPing();
    
    void handleIncomingData();
    bool parseFrame();
    void processMessage(const String& topic, const uint8_t* payload, size_t length, uint64_t sequence);
    
    void resetConnection();
    void updateLastActivity();
    bool checkConnection();
};

#endif // METRICMQ_H
