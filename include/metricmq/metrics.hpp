// include/metricmq/metrics.hpp
#pragma once
#include <atomic>
#include <string>
#include <mutex>
#include <map>
#include <chrono>

namespace metricmq {

// Simple metrics collector for Prometheus
class Metrics {
public:
    static Metrics& instance();
    
    // Counter operations
    void incrementPublished();
    void incrementDelivered();
    void incrementSubscriptions();
    void incrementUnsubscriptions();
    void incrementConnections();
    void incrementDisconnections();
    void incrementAcks();
    void incrementErrors();
    
    // Gauge operations
    void setActiveConnections(int64_t count);
    void setActiveSubscribers(int64_t count);
    void setTopicCount(int64_t count);
    
    // Histogram operations (message latency in microseconds)
    void recordPublishLatency(int64_t latency_us);
    void recordDeliveryLatency(int64_t latency_us);
    
    // Per-topic metrics
    void incrementTopicMessages(const std::string& topic);
    void incrementTopicSubscribers(const std::string& topic);
    void decrementTopicSubscribers(const std::string& topic);
    
    // Export metrics in Prometheus text format
    std::string exportPrometheus() const;
    
    // Reset all metrics (for testing)
    void reset();
    
private:
    Metrics() = default;
    Metrics(const Metrics&) = delete;
    Metrics& operator=(const Metrics&) = delete;
    
    // Counters (monotonically increasing)
    std::atomic<uint64_t> messages_published_total_{0};
    std::atomic<uint64_t> messages_delivered_total_{0};
    std::atomic<uint64_t> subscriptions_total_{0};
    std::atomic<uint64_t> unsubscriptions_total_{0};
    std::atomic<uint64_t> connections_total_{0};
    std::atomic<uint64_t> disconnections_total_{0};
    std::atomic<uint64_t> acks_total_{0};
    std::atomic<uint64_t> errors_total_{0};
    
    // Gauges (current value)
    std::atomic<int64_t> active_connections_{0};
    std::atomic<int64_t> active_subscribers_{0};
    std::atomic<int64_t> topic_count_{0};
    
    // Histograms (latency tracking)
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
    
    // Per-topic counters (protected by mutex)
    mutable std::mutex topic_mutex_;
    std::map<std::string, uint64_t> topic_messages_;
    std::map<std::string, int64_t> topic_subscribers_;
    
    void recordLatency(LatencyHistogram& hist, int64_t latency_us);
    std::string exportHistogram(const std::string& name, const std::string& help, 
                                const LatencyHistogram& hist) const;
};

} // namespace metricmq
