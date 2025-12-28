#include "metricmq/storage/LmdbStorage.hpp"
#include <spdlog/spdlog.h>

namespace metricmq::storage {

LmdbStorage::LmdbStorage(const std::string& path) {
    mdb_env_create(&env_);
    mdb_env_set_maxdbs(env_, 1);
    mdb_env_set_mapsize(env_, 1ULL << 30); // 1 GB
    mdb_env_open(env_, path.c_str(), 0, 0664);

    MDB_txn* txn;
    mdb_txn_begin(env_, nullptr, 0, &txn);
    mdb_dbi_open(txn, nullptr, MDB_CREATE, &dbi_);
    mdb_txn_commit(txn);
}

LmdbStorage::~LmdbStorage() {
    mdb_env_close(env_);
}

void LmdbStorage::save(uint64_t seq, const std::string& topic, const std::string& payload) {
    MDB_txn* txn;
    mdb_txn_begin(env_, nullptr, 0, &txn);

    MDB_val key = {sizeof(seq), &seq};
    std::string value = topic + "\0" + payload;
    MDB_val val = {value.size(), value.data()};

    mdb_put(txn, dbi_, &key, &val, 0);
    mdb_txn_commit(txn);
}

std::vector<std::tuple<uint64_t, std::string, std::string>> LmdbStorage::load_range(uint64_t from, uint64_t to) {
    std::vector<std::tuple<uint64_t, std::string, std::string>> result;
    MDB_txn* txn;
    MDB_cursor* cursor;
    mdb_txn_begin(env_, nullptr, MDB_RDONLY, &txn);
    mdb_cursor_open(txn, dbi_, &cursor);

    MDB_val key, val;
    key.mv_size = sizeof(uint64_t);
    key.mv_data = &from;

    while (mdb_cursor_get(cursor, &key, &val, MDB_SET_RANGE) == 0) {
        uint64_t seq = *(uint64_t*)key.mv_data;
        if (seq > to) break;

        const char* data = static_cast<const char*>(val.mv_data);
        size_t topic_len = std::strlen(data);
        std::string topic(data, topic_len);
        std::string payload(data + topic_len + 1, val.mv_size - topic_len - 1);

        result.emplace_back(seq, topic, payload);

        mdb_cursor_get(cursor, &key, &val, MDB_NEXT);
    }

    mdb_cursor_close(cursor);
    mdb_txn_abort(txn);
    return result;
}

uint64_t LmdbStorage::get_last_seq() const {
    MDB_txn* txn;
    MDB_cursor* cursor;
    mdb_txn_begin(env_, nullptr, MDB_RDONLY, &txn);
    mdb_cursor_open(txn, dbi_, &cursor);

    MDB_val key, val;
    uint64_t last = 0;
    if (mdb_cursor_get(cursor, &key, &val, MDB_LAST) == 0) {
        last = *(uint64_t*)key.mv_data;
    }

    mdb_cursor_close(cursor);
    mdb_txn_abort(txn);
    return last;
}

} // namespace