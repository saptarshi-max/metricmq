// src/storage/LmdbStorage.cpp (Recommended Version)
#include "metricmq/storage/LmdbStorage.hpp"
#include <iostream>
#include <cstring>
#include <chrono>

namespace metricmq {

LmdbStorage::LmdbStorage(const std::string& db_path) {
    mdb_env_create(&env_);
    mdb_env_set_maxdbs(env_, 1);
    mdb_env_set_mapsize(env_, 10485760); // 10MB initial size
    mdb_env_open(env_, db_path.c_str(), MDB_NOSUBDIR | MDB_WRITEMAP, 0664);

    MDB_txn* txn;
    mdb_txn_begin(env_, nullptr, 0, &txn);
    mdb_dbi_open(txn, nullptr, MDB_CREATE, &dbi_);
    mdb_txn_commit(txn);
}

LmdbStorage::~LmdbStorage() {
    if (env_) mdb_env_close(env_);
}

uint64_t LmdbStorage::save(const std::string& topic, const std::string& payload) {
    MDB_txn* txn;
    mdb_txn_begin(env_, nullptr, 0, &txn);

    // Get next sequence ID (stored under key "last_seq")
    MDB_val key, val;
    std::string last_key = "last_seq";
    key.mv_data = last_key.data();
    key.mv_size = last_key.size();

    uint64_t seq_id = 0;
    if (mdb_get(txn, dbi_, &key, &val) == 0) {
        std::memcpy(&seq_id, val.mv_data, sizeof(seq_id));
    }
    seq_id++;

    // Save message
    std::string msg_key = "msg:" + std::to_string(seq_id);
    std::string msg_data = topic + "\0" + payload; // null separator

    MDB_val msg_k, msg_v;
    msg_k.mv_data = msg_key.data();
    msg_k.mv_size = msg_key.size();
    msg_v.mv_data = msg_data.data();
    msg_v.mv_size = msg_data.size();

    mdb_put(txn, dbi_, &msg_k, &msg_v, 0);

    // Update last seq
    val.mv_data = &seq_id;
    val.mv_size = sizeof(seq_id);
    mdb_put(txn, dbi_, &key, &val, 0);

    mdb_txn_commit(txn);

    return seq_id;
}

std::vector<StoredMessage> LmdbStorage::get_after(uint64_t seq_id) {
    std::vector<StoredMessage> messages;

    MDB_txn* txn;
    mdb_txn_begin(env_, nullptr, MDB_RDONLY, &txn);

    MDB_cursor* cursor;
    mdb_cursor_open(txn, dbi_, &cursor);

    MDB_val key, val;
    std::string start_key = "msg:" + std::to_string(seq_id + 1);
    key.mv_data = start_key.data();
    key.mv_size = start_key.size();

    while (mdb_cursor_get(cursor, &key, &val, MDB_NEXT) == 0) {
        if (std::strncmp(static_cast<char*>(key.mv_data), "msg:", 4) != 0) continue;

        std::string data(static_cast<char*>(val.mv_data), val.mv_size);
        size_t sep = data.find('\0');
        if (sep == std::string::npos) continue;

        StoredMessage msg;
        msg.seq_id = std::stoull(std::string(static_cast<char*>(key.mv_data) + 4, key.mv_size - 4));
        msg.topic = data.substr(0, sep);
        msg.payload = data.substr(sep + 1);
        msg.timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        messages.push_back(msg);
    }

    mdb_cursor_close(cursor);
    mdb_txn_abort(txn);

    return messages;
}

uint64_t LmdbStorage::get_last_seq_id() {
    MDB_txn* txn;
    mdb_txn_begin(env_, nullptr, MDB_RDONLY, &txn);

    MDB_val key, val;
    std::string last_key = "last_seq";
    key.mv_data = last_key.data();
    key.mv_size = last_key.size();

    uint64_t seq = 0;
    if (mdb_get(txn, dbi_, &key, &val) == 0) {
        std::memcpy(&seq, val.mv_data, sizeof(seq));
    }

    mdb_txn_abort(txn);
    return seq;
}

} // namespace metricmq