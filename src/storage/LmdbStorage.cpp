// src/storage/LmdbStorage.cpp
#include "LmdbStorage.hpp"
#include <iostream>
#include <cstring>
#include <stdexcept>

namespace metricmq::storage {

LmdbStorage::LmdbStorage(const std::string& db_path) : env_(nullptr), dbi_(0) {
    int rc = mdb_env_create(&env_);
    if (rc != 0) {
        throw std::runtime_error("Failed to create LMDB environment");
    }

    mdb_env_set_maxdbs(env_, 1);
    mdb_env_set_mapsize(env_, 1024ULL * 1024 * 1024); // 1 GB — prevents silent truncation under load

    rc = mdb_env_open(env_, db_path.c_str(), MDB_NOSUBDIR | MDB_WRITEMAP, 0664);
    if (rc != 0) {
        throw std::runtime_error("Failed to open LMDB environment");
    }

    MDB_txn* txn = nullptr;
    rc = mdb_txn_begin(env_, nullptr, 0, &txn);
    if (rc != 0) {
        throw std::runtime_error("Failed to begin transaction");
    }

    rc = mdb_dbi_open(txn, nullptr, MDB_CREATE, &dbi_);
    if (rc != 0) {
        mdb_txn_abort(txn);
        throw std::runtime_error("Failed to open database");
    }

    rc = mdb_txn_commit(txn);
    if (rc != 0) {
        throw std::runtime_error(std::string("Failed to commit init transaction: ") + mdb_strerror(rc));
    }
}

LmdbStorage::~LmdbStorage() {
    if (env_) {
        mdb_env_close(env_);
    }
}

void LmdbStorage::save(uint64_t seq, const std::string& topic, const std::string& payload) {
    MDB_txn* txn = nullptr;
    int rc = mdb_txn_begin(env_, nullptr, 0, &txn);
    if (rc != 0) {
        std::cerr << "Failed to begin transaction\n";
        return;
    }

    // Get next sequence ID (stored under key "last_seq")
    MDB_val key, val;
    std::string last_key = "last_seq";
    key.mv_data = (void*)last_key.data();
    key.mv_size = last_key.size();

    uint64_t next_seq = 1;
    if (mdb_get(txn, dbi_, &key, &val) == 0) {
        if (val.mv_size == sizeof(uint64_t)) {
            std::memcpy(&next_seq, val.mv_data, sizeof(uint64_t));
            next_seq++;
        }
    }

    // Create message key: "msg:<seq>"
    std::string msg_key = "msg:" + std::to_string(next_seq);
    std::string msg_data = topic + "\x00" + payload; // null separator

    MDB_val msg_k, msg_v;
    msg_k.mv_data = (void*)msg_key.data();
    msg_k.mv_size = msg_key.size();
    msg_v.mv_data = (void*)msg_data.data();
    msg_v.mv_size = msg_data.size();

    rc = mdb_put(txn, dbi_, &msg_k, &msg_v, 0);
    if (rc != 0) {
        std::cerr << "Failed to put message: " << mdb_strerror(rc) << "\n";
        mdb_txn_abort(txn);
        return;
    }

    // Update last sequence ID
    val.mv_data = &next_seq;
    val.mv_size = sizeof(uint64_t);
    rc = mdb_put(txn, dbi_, &key, &val, 0);
    if (rc != 0) {
        std::cerr << "Failed to update last_seq: " << mdb_strerror(rc) << "\n";
        mdb_txn_abort(txn);
        return;
    }

    mdb_txn_commit(txn);
}

std::vector<std::tuple<uint64_t, std::string, std::string>> LmdbStorage::load_range(uint64_t from, uint64_t to) {
    std::vector<std::tuple<uint64_t, std::string, std::string>> messages;

    MDB_txn* txn = nullptr;
    int rc = mdb_txn_begin(env_, nullptr, MDB_RDONLY, &txn);
    if (rc != 0) {
        std::cerr << "Failed to begin read transaction\n";
        return messages;
    }

    MDB_cursor* cursor = nullptr;
    rc = mdb_cursor_open(txn, dbi_, &cursor);
    if (rc != 0) {
        std::cerr << "Failed to open cursor\n";
        mdb_txn_abort(txn);
        return messages;
    }

    MDB_val key, val;
    std::string start_key = "msg:" + std::to_string(from);
    key.mv_data = (void*)start_key.data();
    key.mv_size = start_key.size();

    // Start from the computed key
    int cursor_rc = mdb_cursor_get(cursor, &key, &val, MDB_SET_RANGE);
    
    while (cursor_rc == 0) {
        // Check if this is a message key
        std::string key_str(static_cast<char*>(key.mv_data), key.mv_size);
        if (key_str.substr(0, 4) != "msg:") {
            cursor_rc = mdb_cursor_get(cursor, &key, &val, MDB_NEXT);
            continue;
        }

        // Parse the sequence ID from key
        uint64_t seq_id = std::stoull(key_str.substr(4));
        if (seq_id > to) break;

        // Parse message data (topic\0payload)
        std::string data(static_cast<char*>(val.mv_data), val.mv_size);
        size_t sep = data.find('\0');
        if (sep != std::string::npos) {
            std::string topic = data.substr(0, sep);
            std::string payload = data.substr(sep + 1);
            messages.emplace_back(seq_id, topic, payload);
        }

        cursor_rc = mdb_cursor_get(cursor, &key, &val, MDB_NEXT);
    }

    mdb_cursor_close(cursor);
    mdb_txn_abort(txn);

    return messages;
}

uint64_t LmdbStorage::get_last_seq() const {
    MDB_txn* txn = nullptr;
    int rc = mdb_txn_begin(env_, nullptr, MDB_RDONLY, &txn);
    if (rc != 0) {
        std::cerr << "Failed to begin read transaction\n";
        return 0;
    }

    MDB_val key, val;
    std::string last_key = "last_seq";
    key.mv_data = (void*)last_key.data();
    key.mv_size = last_key.size();

    uint64_t seq = 0;
    if (mdb_get(txn, dbi_, &key, &val) == 0 && val.mv_size == sizeof(uint64_t)) {
        std::memcpy(&seq, val.mv_data, sizeof(uint64_t));
    }

    mdb_txn_abort(txn);
    return seq;
}

void LmdbStorage::save_ack(const std::string& client_id, uint64_t sequence) {
    MDB_txn* txn = nullptr;
    int rc = mdb_txn_begin(env_, nullptr, 0, &txn);
    if (rc != 0) {
        std::cerr << "Failed to begin transaction for ACK\n";
        return;
    }

    // Create key: "ack:<client_id>:<sequence>"
    std::string ack_key = "ack:" + client_id + ":" + std::to_string(sequence);
    std::string ack_value = "1";  // Simple flag

    MDB_val key, val;
    key.mv_data = (void*)ack_key.data();
    key.mv_size = ack_key.size();
    val.mv_data = (void*)ack_value.data();
    val.mv_size = ack_value.size();

    rc = mdb_put(txn, dbi_, &key, &val, 0);
    if (rc != 0) {
        std::cerr << "Failed to save ACK: " << mdb_strerror(rc) << "\n";
        mdb_txn_abort(txn);
        return;
    }

    mdb_txn_commit(txn);
}

std::unordered_set<uint64_t> LmdbStorage::load_acks(const std::string& client_id) {
    std::unordered_set<uint64_t> acks;

    MDB_txn* txn = nullptr;
    int rc = mdb_txn_begin(env_, nullptr, MDB_RDONLY, &txn);
    if (rc != 0) {
        std::cerr << "Failed to begin read transaction for ACKs\n";
        return acks;
    }

    MDB_cursor* cursor = nullptr;
    rc = mdb_cursor_open(txn, dbi_, &cursor);
    if (rc != 0) {
        std::cerr << "Failed to open cursor for ACKs\n";
        mdb_txn_abort(txn);
        return acks;
    }

    // Scan for keys starting with "ack:<client_id>:"
    std::string prefix = "ack:" + client_id + ":";
    MDB_val key, val;
    key.mv_data = (void*)prefix.data();
    key.mv_size = prefix.size();

    int cursor_rc = mdb_cursor_get(cursor, &key, &val, MDB_SET_RANGE);
    
    while (cursor_rc == 0) {
        std::string key_str(static_cast<char*>(key.mv_data), key.mv_size);
        
        // Check if key starts with our prefix
        if (key_str.substr(0, prefix.size()) != prefix) {
            break;  // No more ACKs for this client
        }

        // Extract sequence ID from "ack:<client_id>:<seq>"
        std::string seq_str = key_str.substr(prefix.size());
        try {
            uint64_t seq = std::stoull(seq_str);
            acks.insert(seq);
        } catch (...) {
            // Skip malformed keys
        }

        cursor_rc = mdb_cursor_get(cursor, &key, &val, MDB_NEXT);
    }

    mdb_cursor_close(cursor);
    mdb_txn_abort(txn);

    return acks;
}

void LmdbStorage::compact(uint64_t max_seq_to_delete) {
    if (max_seq_to_delete == 0) return;

    MDB_txn* txn = nullptr;
    if (mdb_txn_begin(env_, nullptr, 0, &txn) != 0) return;

    MDB_cursor* cursor = nullptr;
    if (mdb_cursor_open(txn, dbi_, &cursor) != 0) {
        mdb_txn_abort(txn);
        return;
    }

    // Collect keys to delete (can't delete while iterating safely)
    std::string prefix = "msg:";
    MDB_val key, val;
    key.mv_data = (void*)prefix.data();
    key.mv_size = prefix.size();

    std::vector<std::string> to_delete;
    int rc = mdb_cursor_get(cursor, &key, &val, MDB_SET_RANGE);
    while (rc == 0) {
        std::string k(static_cast<char*>(key.mv_data), key.mv_size);
        if (k.size() < 4 || k.substr(0, 4) != "msg:") break;
        try {
            uint64_t seq = std::stoull(k.substr(4));
            if (seq <= max_seq_to_delete) to_delete.push_back(k);
        } catch (...) {}
        rc = mdb_cursor_get(cursor, &key, &val, MDB_NEXT);
    }
    mdb_cursor_close(cursor);

    uint64_t deleted = 0;
    for (const auto& k : to_delete) {
        MDB_val del_key;
        del_key.mv_data = (void*)k.data();
        del_key.mv_size = k.size();
        if (mdb_del(txn, dbi_, &del_key, nullptr) == 0) ++deleted;
    }

    mdb_txn_commit(txn);
    if (deleted > 0) {
        std::cout << "[LmdbStorage] compact: removed " << deleted
                  << " messages (seq <= " << max_seq_to_delete << ")\n";
    }
}

void LmdbStorage::purge_old_acks(uint64_t max_seq_to_delete) {
    if (max_seq_to_delete == 0) return;

    MDB_txn* txn = nullptr;
    if (mdb_txn_begin(env_, nullptr, 0, &txn) != 0) return;

    MDB_cursor* cursor = nullptr;
    if (mdb_cursor_open(txn, dbi_, &cursor) != 0) {
        mdb_txn_abort(txn);
        return;
    }

    // Scan all "ack:" keys
    std::string prefix = "ack:";
    MDB_val key, val;
    key.mv_data = (void*)prefix.data();
    key.mv_size = prefix.size();

    std::vector<std::string> to_delete;
    int rc = mdb_cursor_get(cursor, &key, &val, MDB_SET_RANGE);
    while (rc == 0) {
        std::string k(static_cast<char*>(key.mv_data), key.mv_size);
        if (k.size() < 4 || k.substr(0, 4) != "ack:") break;
        // key format: "ack:<client_id>:<seq>" — find last ':'
        size_t last_colon = k.rfind(':');
        if (last_colon != std::string::npos && last_colon > 3) {
            try {
                uint64_t seq = std::stoull(k.substr(last_colon + 1));
                if (seq <= max_seq_to_delete) to_delete.push_back(k);
            } catch (...) {}
        }
        rc = mdb_cursor_get(cursor, &key, &val, MDB_NEXT);
    }
    mdb_cursor_close(cursor);

    for (const auto& k : to_delete) {
        MDB_val del_key;
        del_key.mv_data = (void*)k.data();
        del_key.mv_size = k.size();
        mdb_del(txn, dbi_, &del_key, nullptr);
    }

    mdb_txn_commit(txn);
}

} // namespace metricmq::storage