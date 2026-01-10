// Queue Mode: PUSH/PULL for load-balanced task distribution
#pragma once
#include <string>
#include <functional>

namespace metricmq {

class QueueProducer {
public:
    QueueProducer(const std::string& host = "127.0.0.1", int port = 6379);
    ~QueueProducer();

    // Push task to queue (FIFO)
    void push(const std::string& queue_name, const std::string& payload);

private:
    int sock_ = -1;
    uint64_t sequence_ = 0;
};

class QueueConsumer {
public:
    QueueConsumer(const std::string& host = "127.0.0.1", int port = 6379);
    ~QueueConsumer();

    // Pull task from queue (blocking, round-robin delivery)
    void pull(const std::string& queue_name,
              std::function<void(const std::string& payload)> callback);

    void run();  // Debug mode: print received messages

private:
    int sock_ = -1;
    uint64_t sequence_ = 0;
};

} // namespace metricmq
