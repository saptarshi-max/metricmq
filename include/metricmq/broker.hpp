/**
 * @file broker.hpp
 * @brief Public broker facade — the only type a server application needs.
 *
 * Full implementation lives in src/broker.hpp. This header exposes only
 * the interface needed by main().
 *
 * @par Usage
 * @code
 * metricmq::Broker broker(6379);
 * broker.run();  // blocks until SIGINT/SIGTERM
 * @endcode
 */
#pragma once
#include <string>

namespace metricmq {

/**
 * @brief TCP message broker (public facade).
 *
 * Accepts connections, auto-detects RESP vs binary protocol per connection,
 * and routes messages to all subscribers. Call run() on the main thread.
 */
class Broker {
public:
    /**
     * @brief Construct the broker bound to the given port.
     * @param port TCP port to listen on (default: 6379).
     */
    explicit Broker(int port = 6379);

    /**
     * @brief Start accepting connections. Blocks until stop() is called.
     *
     * Each accepted connection spawns a detached Session thread.
     * Uses a 1-second select() timeout to check the shutdown flag.
     */
    void run();
};

} // namespace metricmq
