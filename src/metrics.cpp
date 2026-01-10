// src/metrics.cpp
#include "metricmq/metrics.hpp"
#include <sstream>
#include <iomanip>

namespace metricmq {

Metrics& Metrics::instance() {
    static Metrics instance;
    return instance;
}

void Metrics::incrementPublished() {
    messages_published_total_.fetch_add(1, std::memory_order_relaxed);
}

void Metrics::incrementDelivered() {
    messages_delivered_total_.fetch_add(1, std::memory_order_relaxed);
}

void Metrics::incrementSubscriptions() {
    subscriptions_total_.fetch_add(1, std::memory_order_relaxed);
}

void Metrics::incrementUnsubscriptions() {
    unsubscriptions_total_.fetch_add(1, std::memory_order_relaxed);
}

void Metrics::incrementConnections() {
    connections_total_.fetch_add(1, std::memory_order_relaxed);
}

void Metrics::incrementDisconnections() {
    disconnections_total_.fetch_add(1, std::memory_order_relaxed);
}

void Metrics::incrementAcks() {
    acks_total_.fetch_add(1, std::memory_order_relaxed);
}

void Metrics::incrementErrors() {
    errors_total_.fetch_add(1, std::memory_order_relaxed);
}

void Metrics::setActiveConnections(int64_t count) {
    active_connections_.store(count, std::memory_order_relaxed);
}

void Metrics::setActiveSubscribers(int64_t count) {
    active_subscribers_.store(count, std::memory_order_relaxed);
}

void Metrics::setTopicCount(int64_t count) {
    topic_count_.store(count, std::memory_order_relaxed);
}

void Metrics::recordLatency(LatencyHistogram& hist, int64_t latency_us) {
    hist.count.fetch_add(1, std::memory_order_relaxed);
    hist.sum_us.fetch_add(latency_us, std::memory_order_relaxed);
    
    if (latency_us < 100) {
        hist.bucket_lt_100us.fetch_add(1, std::memory_order_relaxed);
    } else if (latency_us < 500) {
        hist.bucket_lt_500us.fetch_add(1, std::memory_order_relaxed);
    } else if (latency_us < 1000) {
        hist.bucket_lt_1ms.fetch_add(1, std::memory_order_relaxed);
    } else if (latency_us < 5000) {
        hist.bucket_lt_5ms.fetch_add(1, std::memory_order_relaxed);
    } else if (latency_us < 10000) {
        hist.bucket_lt_10ms.fetch_add(1, std::memory_order_relaxed);
    } else if (latency_us < 50000) {
        hist.bucket_lt_50ms.fetch_add(1, std::memory_order_relaxed);
    } else if (latency_us < 100000) {
        hist.bucket_lt_100ms.fetch_add(1, std::memory_order_relaxed);
    } else {
        hist.bucket_inf.fetch_add(1, std::memory_order_relaxed);
    }
}

void Metrics::recordPublishLatency(int64_t latency_us) {
    recordLatency(publish_latency_, latency_us);
}

void Metrics::recordDeliveryLatency(int64_t latency_us) {
    recordLatency(delivery_latency_, latency_us);
}

void Metrics::incrementTopicMessages(const std::string& topic) {
    std::lock_guard<std::mutex> lock(topic_mutex_);
    topic_messages_[topic]++;
}

void Metrics::incrementTopicSubscribers(const std::string& topic) {
    std::lock_guard<std::mutex> lock(topic_mutex_);
    topic_subscribers_[topic]++;
}

void Metrics::decrementTopicSubscribers(const std::string& topic) {
    std::lock_guard<std::mutex> lock(topic_mutex_);
    auto it = topic_subscribers_.find(topic);
    if (it != topic_subscribers_.end()) {
        it->second--;
        if (it->second <= 0) {
            topic_subscribers_.erase(it);
        }
    }
}

std::string Metrics::exportHistogram(const std::string& name, const std::string& help,
                                     const LatencyHistogram& hist) const {
    std::ostringstream oss;
    
    uint64_t count = hist.count.load(std::memory_order_relaxed);
    uint64_t sum = hist.sum_us.load(std::memory_order_relaxed);
    
    oss << "# HELP " << name << " " << help << "\n";
    oss << "# TYPE " << name << " histogram\n";
    
    // Cumulative buckets
    uint64_t cumulative = 0;
    
    cumulative += hist.bucket_lt_100us.load(std::memory_order_relaxed);
    oss << name << "_bucket{le=\"0.0001\"} " << cumulative << "\n";
    
    cumulative += hist.bucket_lt_500us.load(std::memory_order_relaxed);
    oss << name << "_bucket{le=\"0.0005\"} " << cumulative << "\n";
    
    cumulative += hist.bucket_lt_1ms.load(std::memory_order_relaxed);
    oss << name << "_bucket{le=\"0.001\"} " << cumulative << "\n";
    
    cumulative += hist.bucket_lt_5ms.load(std::memory_order_relaxed);
    oss << name << "_bucket{le=\"0.005\"} " << cumulative << "\n";
    
    cumulative += hist.bucket_lt_10ms.load(std::memory_order_relaxed);
    oss << name << "_bucket{le=\"0.01\"} " << cumulative << "\n";
    
    cumulative += hist.bucket_lt_50ms.load(std::memory_order_relaxed);
    oss << name << "_bucket{le=\"0.05\"} " << cumulative << "\n";
    
    cumulative += hist.bucket_lt_100ms.load(std::memory_order_relaxed);
    oss << name << "_bucket{le=\"0.1\"} " << cumulative << "\n";
    
    cumulative += hist.bucket_inf.load(std::memory_order_relaxed);
    oss << name << "_bucket{le=\"+Inf\"} " << cumulative << "\n";
    
    oss << name << "_sum " << std::fixed << std::setprecision(6) << (sum / 1000000.0) << "\n";
    oss << name << "_count " << count << "\n";
    
    return oss.str();
}

std::string Metrics::exportPrometheus() const {
    std::ostringstream oss;
    
    // Counters
    oss << "# HELP metricmq_messages_published_total Total number of messages published\n";
    oss << "# TYPE metricmq_messages_published_total counter\n";
    oss << "metricmq_messages_published_total " << messages_published_total_.load(std::memory_order_relaxed) << "\n\n";
    
    oss << "# HELP metricmq_messages_delivered_total Total number of messages delivered to subscribers\n";
    oss << "# TYPE metricmq_messages_delivered_total counter\n";
    oss << "metricmq_messages_delivered_total " << messages_delivered_total_.load(std::memory_order_relaxed) << "\n\n";
    
    oss << "# HELP metricmq_subscriptions_total Total number of subscriptions\n";
    oss << "# TYPE metricmq_subscriptions_total counter\n";
    oss << "metricmq_subscriptions_total " << subscriptions_total_.load(std::memory_order_relaxed) << "\n\n";
    
    oss << "# HELP metricmq_unsubscriptions_total Total number of unsubscriptions\n";
    oss << "# TYPE metricmq_unsubscriptions_total counter\n";
    oss << "metricmq_unsubscriptions_total " << unsubscriptions_total_.load(std::memory_order_relaxed) << "\n\n";
    
    oss << "# HELP metricmq_connections_total Total number of client connections\n";
    oss << "# TYPE metricmq_connections_total counter\n";
    oss << "metricmq_connections_total " << connections_total_.load(std::memory_order_relaxed) << "\n\n";
    
    oss << "# HELP metricmq_disconnections_total Total number of client disconnections\n";
    oss << "# TYPE metricmq_disconnections_total counter\n";
    oss << "metricmq_disconnections_total " << disconnections_total_.load(std::memory_order_relaxed) << "\n\n";
    
    oss << "# HELP metricmq_acks_total Total number of message acknowledgements\n";
    oss << "# TYPE metricmq_acks_total counter\n";
    oss << "metricmq_acks_total " << acks_total_.load(std::memory_order_relaxed) << "\n\n";
    
    oss << "# HELP metricmq_errors_total Total number of errors\n";
    oss << "# TYPE metricmq_errors_total counter\n";
    oss << "metricmq_errors_total " << errors_total_.load(std::memory_order_relaxed) << "\n\n";
    
    // Gauges
    oss << "# HELP metricmq_active_connections Current number of active connections\n";
    oss << "# TYPE metricmq_active_connections gauge\n";
    oss << "metricmq_active_connections " << active_connections_.load(std::memory_order_relaxed) << "\n\n";
    
    oss << "# HELP metricmq_active_subscribers Current number of active subscribers\n";
    oss << "# TYPE metricmq_active_subscribers gauge\n";
    oss << "metricmq_active_subscribers " << active_subscribers_.load(std::memory_order_relaxed) << "\n\n";
    
    oss << "# HELP metricmq_topics Current number of topics with subscribers\n";
    oss << "# TYPE metricmq_topics gauge\n";
    oss << "metricmq_topics " << topic_count_.load(std::memory_order_relaxed) << "\n\n";
    
    // Histograms
    oss << exportHistogram("metricmq_publish_latency_seconds", 
                          "Publish operation latency in seconds", 
                          publish_latency_);
    oss << "\n";
    
    oss << exportHistogram("metricmq_delivery_latency_seconds",
                          "Message delivery latency in seconds",
                          delivery_latency_);
    oss << "\n";
    
    // Per-topic metrics
    {
        std::lock_guard<std::mutex> lock(topic_mutex_);
        
        if (!topic_messages_.empty()) {
            oss << "# HELP metricmq_topic_messages_total Messages published per topic\n";
            oss << "# TYPE metricmq_topic_messages_total counter\n";
            for (const auto& [topic, count] : topic_messages_) {
                oss << "metricmq_topic_messages_total{topic=\"" << topic << "\"} " << count << "\n";
            }
            oss << "\n";
        }
        
        if (!topic_subscribers_.empty()) {
            oss << "# HELP metricmq_topic_subscribers Current subscribers per topic\n";
            oss << "# TYPE metricmq_topic_subscribers gauge\n";
            for (const auto& [topic, count] : topic_subscribers_) {
                oss << "metricmq_topic_subscribers{topic=\"" << topic << "\"} " << count << "\n";
            }
            oss << "\n";
        }
    }
    
    return oss.str();
}

void Metrics::reset() {
    messages_published_total_.store(0, std::memory_order_relaxed);
    messages_delivered_total_.store(0, std::memory_order_relaxed);
    subscriptions_total_.store(0, std::memory_order_relaxed);
    unsubscriptions_total_.store(0, std::memory_order_relaxed);
    connections_total_.store(0, std::memory_order_relaxed);
    disconnections_total_.store(0, std::memory_order_relaxed);
    acks_total_.store(0, std::memory_order_relaxed);
    errors_total_.store(0, std::memory_order_relaxed);
    
    active_connections_.store(0, std::memory_order_relaxed);
    active_subscribers_.store(0, std::memory_order_relaxed);
    topic_count_.store(0, std::memory_order_relaxed);
    
    std::lock_guard<std::mutex> lock(topic_mutex_);
    topic_messages_.clear();
    topic_subscribers_.clear();
}

} // namespace metricmq
