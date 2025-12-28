// src/broker.hpp
#pragma once
#include <vector>
#include <memory>

namespace metricmq {

class Session;

class Broker {
public:
    explicit Broker(int port = 6379);
    ~Broker();
    void run();  // blocking
    
    friend class Session;  // Allow Session to access private members

private:
    int port_;
    int server_fd_;
    std::vector<std::shared_ptr<Session>> sessions_;
};

} // namespace metricmq