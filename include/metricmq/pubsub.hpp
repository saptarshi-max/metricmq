#pragma once
#include <string>
#include <functional>
#include <map>

namespace metricmq {

class Publisher {
public:
    Publisher(const std::string& host = "127.0.0.1", int port = 6379);
    ~Publisher();

    void send(const std::string& topic, const std::string& payload);

private:
    int sock_ = -1;
};

class Subscriber {
public:
    Subscriber(const std::string& host = "127.0.0.1", int port = 6379);
    ~Subscriber();

    // Callback receives both topic and payload (matches your implementation)
    void subscribe(const std::string& topic,
                   std::function<void(const std::string& topic, const std::string& payload)> callback);

    void run();  // blocking receive loop

private:
    int sock_ = -1;
    
    // Exactly-once: Track last processed sequence per topic
    std::map<std::string, uint64_t> last_seq_;
    
    // Helper to send ACK
    void sendAck(uint64_t sequence);
};

} // namespace metricmq