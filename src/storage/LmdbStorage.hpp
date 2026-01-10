#pragma once
#include <string>
#include <vector>
#include <unordered_set>
#include <lmdb.h>

namespace metricmq::storage {

class LmdbStorage {
public:
    LmdbStorage(const std::string& path = "metricmq.db");
    ~LmdbStorage();

    void save(uint64_t seq, const std::string& topic, const std::string& payload);
    std::vector<std::tuple<uint64_t, std::string, std::string>> load_range(uint64_t from, uint64_t to);
    uint64_t get_last_seq() const;
    
    // ACK tracking
    void save_ack(const std::string& client_id, uint64_t sequence);
    std::unordered_set<uint64_t> load_acks(const std::string& client_id);

private:
    MDB_env* env_;
    MDB_dbi dbi_;
};

} // namespace