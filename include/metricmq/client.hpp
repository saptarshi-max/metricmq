// Public PUB/SUB Broker

#pragma once
#include <string>
#include <functional>

namespace metricmq {

class Publisher {
public:
    Publisher(const std::string& host = "127.0.0.1", int port = 6379);
    void send(const std::string& topic, const std::string& payload);
};

class Subscriber {
public:
    Subscriber(const std::string& host = "127.0.0.1", int port = 6379);
    // Callback receives both topic and payload (match implementation)
    void subscribe(const std::string& topic, std::function<void(const std::string& topic, const std::string& payload)> callback);
    void run();  // blocking receive
};

} // namespace metricmq