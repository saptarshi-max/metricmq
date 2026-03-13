/**
 * @file pubsub.hpp
 * @brief RESP (Redis-compatible) text-protocol publisher and subscriber.
 *
 * Use this API when connecting from tools that speak the Redis protocol
 * (redis-cli, redis-py, etc.) or when binary protocol overhead is not needed.
 *
 * @note For higher throughput and Ed25519 signing, prefer `binary_pubsub.hpp`.
 */
#pragma once
#include <string>
#include <functional>
#include <map>

namespace metricmq {

/**
 * @brief RESP-protocol message publisher.
 *
 * Sends `PUBLISH topic payload` commands over a persistent TCP connection.
 *
 * @par Example
 * @code
 * metricmq::Publisher pub("127.0.0.1", 6379);
 * pub.send("sensors/temp", "22.5");
 * @endcode
 *
 * @note Not thread-safe. Use one instance per thread.
 */
class Publisher {
public:
    /**
     * @brief Open a TCP connection to the broker.
     * @param host Broker hostname or IP (default: `"127.0.0.1"`).
     * @param port Broker TCP port (default: 6379).
     * @throws std::runtime_error if connection fails.
     */
    Publisher(const std::string& host = "127.0.0.1", int port = 6379);

    /** @brief Close the TCP connection. */
    ~Publisher();

    /**
     * @brief Publish one message using the RESP PUBLISH command.
     * @param topic   Destination topic (max 256 bytes).
     * @param payload Message body (max 16 MB).
     */
    void send(const std::string& topic, const std::string& payload);

private:
    int sock_ = -1;
};

/**
 * @brief RESP-protocol message subscriber with built-in exactly-once ACK.
 *
 * Call `subscribe()` to register a topic and callback, then call `run()` —
 * or just call `subscribe()` directly (it runs the receive loop internally).
 *
 * The subscriber automatically sends ACK frames using per-topic sequence tracking
 * stored in `last_seq_`.
 *
 * @par Example
 * @code
 * metricmq::Subscriber sub("127.0.0.1", 6379);
 * sub.subscribe("sensors/temp", [](const std::string& topic, const std::string& payload) {
 *     std::cout << payload << "\n";
 * });
 * // subscribe() blocks here
 * @endcode
 */
class Subscriber {
public:
    /**
     * @brief Open a TCP connection to the broker.
     * @param host Broker hostname or IP (default: `"127.0.0.1"`).
     * @param port Broker TCP port (default: 6379).
     */
    Subscriber(const std::string& host = "127.0.0.1", int port = 6379);

    /** @brief Close the TCP connection. */
    ~Subscriber();

    /**
     * @brief Subscribe to a topic and block until the connection closes.
     *
     * @param topic    Topic to subscribe to. Use `"#"` for wildcard.
     * @param callback Invoked with `(topic, payload)` for each message delivered.
     *                 Called on the calling thread.
     */
    void subscribe(const std::string& topic,
                   std::function<void(const std::string& topic, const std::string& payload)> callback);

    /**
     * @brief Run a raw receive loop that prints all incoming frames.
     * @note For debugging only. Prefer `subscribe()` with a callback.
     */
    void run();

private:
    int sock_ = -1;
    std::map<std::string, uint64_t> last_seq_; ///< Per-topic last ACK'd sequence
    void sendAck(uint64_t sequence);
};

} // namespace metricmq