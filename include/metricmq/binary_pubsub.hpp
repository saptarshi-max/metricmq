// Binary Protocol Publisher and Subscriber
#pragma once
#include <string>
#include <functional>
#include <cstdint>

namespace metricmq {

class BinaryPublisher {
public:
    BinaryPublisher(const std::string& host = "127.0.0.1", int port = 6379);
    ~BinaryPublisher();

    void send(const std::string& topic, const std::string& payload);

private:
    int sock_ = -1;
    uint64_t sequence_ = 0;
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

    void run();  // blocking receive loop (for debugging)

private:
    void sendAck(uint64_t sequence);
    
    int sock_ = -1;
    uint64_t sequence_ = 0;
    std::string client_id_;
    bool auto_ack_ = true;
};

} // namespace metricmq
