/**
 * @file LmdbStorage.hpp
 * @brief LMDB-backed persistence for messages and ACK state.
 *
 * All published messages are written before fan-out. On broker restart,
 * subscribers with a client_id replay messages since their last ACK.
 *
 * @par Key schema (flat LMDB namespace)
 * | Key | Value | Description |
 * |---|---|---|
 * |  |  (8 B) | Highest sequence written |
 * |  |  | Published message |
 * |  |  | Per-client ACK record |
 *
 * @par Configuration
 * Map size: 1 GB  |  Flags: MDB_NOSUBDIR | MDB_WRITEMAP
 *
 * @par Thread safety
 * Not thread-safe � all calls must be serialized (by the broker mutex).
 */
#pragma once
#include <string>
#include <vector>
#include <tuple>
#include <unordered_set>
#include <lmdb.h>

namespace metricmq::storage {

/**
 * @brief LMDB wrapper for MetricMQ message and ACK persistence.
 *
 * Construction opens/creates the DB file. Destruction closes the environment
 * and flushes all pending writes (MDB_WRITEMAP guarantees OS page writeback).
 */
class LmdbStorage {
public:
    /**
     * @brief Open or create an LMDB database.
     * @param db_path Path to the LMDB data file (e.g. "metricmq.db").
     * @throws std::runtime_error if the environment cannot be created or opened.
     */
    explicit LmdbStorage(const std::string& db_path);

    /** @brief Close the LMDB environment (flushes pending writes to disk). */
    ~LmdbStorage();

    LmdbStorage(const LmdbStorage&) = delete;
    LmdbStorage& operator=(const LmdbStorage&) = delete;

    /**
     * @brief Persist one published message.
     *
     * Writes  =  and updates .
     *
     * @param seq     Broker-assigned sequence ID.
     * @param topic   Message topic string.
     * @param payload Message body bytes.
     */
    void save(uint64_t seq, const std::string& topic, const std::string& payload);

    /**
     * @brief Load a range of messages for subscriber replay.
     *
     * @param from First sequence ID (inclusive).
     * @param to   Last sequence ID (inclusive). Typical call: .
     * @return Vector of (seq, topic, payload) in ascending seq order.
     */
    std::vector<std::tuple<uint64_t, std::string, std::string>>
    load_range(uint64_t from, uint64_t to);

    /**
     * @brief Return the highest persisted sequence ID.
     *
     * Read at startup to restore the in-memory counter without loading all messages.
     * @return Value of the  record, or 0 if the database is empty.
     */
    uint64_t get_last_seq() const;

    /**
     * @brief Record a client ACK for a given sequence ID.
     *
     * Writes  =  to LMDB.
     *
     * @param client_id Unique client identifier.
     * @param sequence  The sequence ID being acknowledged.
     */
    void save_ack(const std::string& client_id, uint64_t sequence);

    /**
     * @brief Load all ACK'd sequences for a reconnecting client.
     *
     * Scans keys with prefix .
     *
     * @param client_id Reconnecting client's identifier.
     * @return Set of all sequence IDs this client has acknowledged.
     */
    std::unordered_set<uint64_t> load_acks(const std::string& client_id);

    /**
     * @brief Delete old messages to keep LMDB below its size limit.
     *
     * Removes all `msg:<seq>` records where seq <= @p max_seq_to_delete.
     * Call periodically (e.g. every 1 000 publishes) to prevent
     * `MDB_MAP_FULL` crashes on long-running deployments.
     *
     * @param max_seq_to_delete Highest sequence number to purge (inclusive).
     */
    void compact(uint64_t max_seq_to_delete);

    /**
     * @brief Delete stale ACK records that are no longer needed.
     *
     * Removes all `ack:<client>:<seq>` records where seq <= @p max_seq_to_delete.
     * Call with the same threshold as compact() so ACK records for compacted
     * messages do not accumulate indefinitely.
     *
     * @param max_seq_to_delete Highest sequence number whose ACK records can be dropped.
     */
    void purge_old_acks(uint64_t max_seq_to_delete);

private:
    MDB_env* env_; ///< LMDB environment handle.
    MDB_dbi  dbi_; ///< Database handle (single flat namespace).
};

} // namespace metricmq::storage
