#pragma once
#include <string>

namespace metricmq {

class Broker;

class Session {
public:
    Session(int sock_fd, Broker* broker);
    void run();  // blocking receive loop
    void send(const std::string& data);

private:
    int sock_fd_;
    Broker* broker_;
};

} // namespace metricmq