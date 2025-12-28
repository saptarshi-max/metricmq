#pragma once
#include <string>

namespace metricmq {

class Broker {
public:
    explicit Broker(int port = 6379);
    void run();  // starts the server
};

} // namespace metricmq