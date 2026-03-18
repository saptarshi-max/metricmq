/**
 * @file queue.hpp
 * @brief PUSH/PULL task queue API for load-balanced work distribution.
 *
 * Queue semantics are implemented at the broker by round-robin delivery to
 * multiple consumers subscribed to the same "queue" topic. Each task is
 * delivered to exactly one consumer.
 *
 * @par Example — producer
 * @code
 * metricmq::QueueProducer prod("127.0.0.1", 6379);
 * prod.push("tasks/resize", R"({"image":"photo.jpg","width":800})");
 * @endcode
 *
 * @par Example — consumer (run multiple for parallelism)
 * @code
 * metricmq::QueueConsumer cons("127.0.0.1", 6379);
 * cons.pull("tasks/resize", [](const std::string& payload) {
 *     process(payload);   // called once per task
 * });
 * @endcode
 */
#pragma once
#include <string>
#include <functional>

namespace metricmq {

/**
 * @brief Pushes tasks onto a named queue (binary protocol, PUBLISH semantics).
 *
 * Each `push()` call publishes one message. The broker delivers it to one of
 * the subscribed consumers in round-robin order.
 *
 * @note Not thread-safe. Use one instance per thread.
 */
class QueueProducer {
public:
    /**
     * @brief Connect to the broker.
     * @param host Broker hostname or IP (default: `"127.0.0.1"`).
     * @param port Broker TCP port (default: 6379).
     */
    QueueProducer(const std::string& host = "127.0.0.1", int port = 6379);
    ~QueueProducer();

    /**
     * @brief Enqueue one task.
     * @param queue_name The queue identifier (used as the topic name).
     * @param payload    Task data (arbitrary bytes, max 16 MB).
     */
    void push(const std::string& queue_name, const std::string& payload);

private:
    int sock_ = -1;
    uint64_t sequence_ = 0;
};

/**
 * @brief Pulls tasks from a named queue (blocking, binary protocol).
 *
 * Sends a SUBSCRIBE to the queue topic and invokes `callback` for each
 * delivered task. Run multiple `QueueConsumer` instances for parallel processing.
 *
 * @note `pull()` is blocking. Run it on a dedicated thread per consumer.
 * @note Not thread-safe. Use one instance per thread.
 */
class QueueConsumer {
public:
    /**
     * @brief Connect to the broker.
     * @param host Broker hostname or IP (default: `"127.0.0.1"`).
     * @param port Broker TCP port (default: 6379).
     */
    QueueConsumer(const std::string& host = "127.0.0.1", int port = 6379);
    ~QueueConsumer();

    /**
     * @brief Subscribe to a queue and block, invoking callback for each task.
     *
     * @param queue_name The queue to consume (same name passed to `QueueProducer::push()`).
     * @param callback   Called with the raw payload string for each received task.
     */
    void pull(const std::string& queue_name,
              std::function<void(const std::string& payload)> callback);

    /**
     * @brief Raw receive loop, prints frames.
     * @note For debugging only. Prefer `pull()` with a callback.
     */
    void run();

private:
    int sock_ = -1;
    uint64_t sequence_ = 0;
};

} // namespace metricmq
