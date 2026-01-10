// src/broker.hpp
#pragma once
#include <vector>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <atomic>
#include "storage/LmdbStorage.hpp"

namespace metricmq {

class Session;

class Broker {
public:
    explicit Broker(int port = 6379);
    ~Broker();
    void run();  // blocking
    void stop();  // graceful shutdown
    
    // Topic subscription management
    void subscribe(Session* session, const std::string& topic);
    void unsubscribe(Session* session, const std::string& topic);
    uint64_t publish(const std::string& topic, const std::string& payload);  // Returns sequence ID
    
    // Exactly-once semantics
    void handleAck(const std::string& client_id, uint64_t sequence);
    uint64_t getLastSequence() const;
    uint64_t getLastAck(const std::string& client_id) const;
    bool isAcked(const std::string& client_id, uint64_t sequence) const;
    
    // Replay messages for newly subscribed session
    void replayMessages(Session* session, const std::string& topic, uint64_t from_seq = 0);
    void replayMessagesForClient(Session* session, const std::string& topic, const std::string& client_id);
    
    // Client management
    void registerClient(const std::string& client_id, Session* session);
    void unregisterClient(const std::string& client_id);
    
    // Session management
    void removeSession(Session* session);
    
    friend class Session;  // Allow Session to access private members

private:
    int countTotalSubscribers() const;  // Helper for metrics
    
    int port_;
    int server_fd_;
    std::vector<std::shared_ptr<Session>> sessions_;
    
    // Topic -> Set of subscribers
    std::unordered_map<std::string, std::unordered_set<Session*>> topic_subscribers_;
    
    // Persistence layer
    std::unique_ptr<storage::LmdbStorage> persistence_;
    
    // Exactly-once: Per-client ACK tracking
    // client_id -> set of ACK'd sequences
    std::unordered_map<std::string, std::unordered_set<uint64_t>> client_acks_;
    // client_id -> last ACK'd sequence (optimization for sequential access)
    std::unordered_map<std::string, uint64_t> last_ack_seq_;
    // client_id -> session mapping
    std::unordered_map<std::string, Session*> client_sessions_;
    
    uint64_t current_sequence_;
    
    // Shutdown flag
    std::atomic<bool> shutdown_requested_;
    
    // Mutex for thread-safe access
    mutable std::mutex mutex_;
};

} // namespace metricmq