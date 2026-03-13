/**
 * @file metrics.hpp
 * @brief Prometheus-compatible metrics singleton for the MetricMQ broker.
 *
 * `Metrics` is a process-wide singleton. Every broker component calls into it
 * via `Metrics::instance()`. `MetricsServer` calls `exportPrometheus()` on
 * every `GET /metrics` request and serves the result as Prometheus text format.
 *
 * @par Metric types
 * - **Counters** — monotonically increasing totals (messages, connections, ACKs).
 * - **Gauges** — current snapshot values (active connections, topic count).
 * - **Histograms** — latency distributions with fixed buckets (µs resolution).
 *
 * @par Thread safety
 * Counter and gauge updates use `std::atomic`. Per-topic maps use a dedicated mutex.
 * `exportPrometheus()` acquires the topic mutex and reads all atomics.
 */
// include/metricmq/metrics.hpp
#pragma once
#include <atomic>
#include <string>
#include <mutex>
#include <map>
#include <chrono>

namespace metricmq {

/**
 * @brief Lock-free metrics collector with Prometheus text export.
 *
 * @par Histogram buckets (latency in seconds)
 * `< 0.0001`  `< 0.0005`  `< 0.001`  `< 0.005`  `< 0.010`  `< 0.050`  `< 0.100`  `+Inf`
 */
class Metrics {
public:
    /** @brief Access the process-wide singleton. */
    static Metrics& instance();

    /// @name Counters (monotonically increasing)
    /// @{
    void incrementPublished();      ///< A message was received from a publisher.
    void incrementDelivered();      ///< A message was delivered to a subscriber.
    void incrementSubscriptions();  ///< A SUBSCRIBE command was processed.
    void incrementUnsubscriptions();///< An UNSUBSCRIBE command was processed.
    void incrementConnections();    ///< A new TCP connection was accepted.
    void incrementDisconnections(); ///< A TCP connection was closed.
    void incrementAcks();           ///< An ACK frame was processed.
    void incrementErrors();         ///< An error response was sent.
    /// @}

    /// @name Gauges (current snapshot)
    /// @{
    void setActiveConnections(int64_t count); ///< Update current open connection count.
    void setActiveSubscribers(int64_t count); ///< Update current subscription count.
    void setTopicCount(int64_t count);        ///< Update number of distinct active topics.
    /// @}

    /// @name Histograms
    /// @{
    /** @brief Record end-to-end publish latency in microseconds. */
    void recordPublishLatency(int64_t latency_us);
    /** @brief Record message delivery latency in microseconds. */
    void recordDeliveryLatency(int64_t latency_us);
    /// @}

    /// @name Per-topic metrics
    /// @{
    void incrementTopicMessages(const std::string& topic);    ///< One message published to topic.
    void incrementTopicSubscribers(const std::string& topic); ///< One new subscriber on topic.
    void decrementTopicSubscribers(const std::string& topic); ///< One subscriber left topic.
    /// @}

    /**
     * @brief Serialize all metrics to Prometheus text exposition format.
     *
     * Called by `MetricsServer` on every `GET /metrics` request.
     * Output is compatible with Prometheus 0.0.4 text format.
     *
     * @return Multi-line string with `# HELP`, `# TYPE`, and metric lines.
     */
    std::string exportPrometheus() const;

    /**
     * @brief Reset all counters, gauges, and histograms to zero.
     * @note Intended for unit testing only.
     */
    void reset();

private:
    Metrics() = default;
    Metrics(const Metrics&) = delete;
    Metrics& operator=(const Metrics&) = delete;

    // Counters
    std::atomic<uint64_t> messages_published_total_{0};
    std::atomic<uint64_t> messages_delivered_total_{0};
    std::atomic<uint64_t> subscriptions_total_{0};
    std::atomic<uint64_t> unsubscriptions_total_{0};
    std::atomic<uint64_t> connections_total_{0};
    std::atomic<uint64_t> disconnections_total_{0};
    std::atomic<uint64_t> acks_total_{0};
    std::atomic<uint64_t> errors_total_{0};

    // Gauges
    std::atomic<int64_t> active_connections_{0};
    std::atomic<int64_t> active_subscribers_{0};
    std::atomic<int64_t> topic_count_{0};

    /** @brief Internal histogram with 8 fixed latency buckets. */
    struct LatencyHistogram {
        std::atomic<uint64_t> count{0};
        std::atomic<uint64_t> sum_us{0};
        std::atomic<uint64_t> bucket_lt_100us{0};
        std::atomic<uint64_t> bucket_lt_500us{0};
        std::atomic<uint64_t> bucket_lt_1ms{0};
        std::atomic<uint64_t> bucket_lt_5ms{0};
        std::atomic<uint64_t> bucket_lt_10ms{0};
        std::atomic<uint64_t> bucket_lt_50ms{0};
        std::atomic<uint64_t> bucket_lt_100ms{0};
        std::atomic<uint64_t> bucket_inf{0};
    };

    LatencyHistogram publish_latency_;
    LatencyHistogram delivery_latency_;

    mutable std::mutex topic_mutex_;
    std::map<std::string, uint64_t> topic_messages_;
    std::map<std::string, int64_t>  topic_subscribers_;

    void recordLatency(LatencyHistogram& hist, int64_t latency_us);
    std::string exportHistogram(const std::string& name, const std::string& help,
                                const LatencyHistogram& hist) const;
};

} // namespace metricmq
