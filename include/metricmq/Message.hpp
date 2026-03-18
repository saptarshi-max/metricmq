/**
 * @file Message.hpp
 * @brief Core message value type carrying a topic and a payload.
 *
 * `Message` is the minimal data object passed between internal broker components.
 * The RESP and binary protocol layers both decompose into `Message` values before
 * calling into the broker's routing logic.
 */
#pragma once
#include <string>

namespace metricmq {

/**
 * @brief A routable message with a topic and an arbitrary byte payload.
 *
 * @par Fields
 * - `topic`   — UTF-8 topic string. Used by the broker for exact-match routing
 *               and wildcard (`"#"`) delivery. Maximum 256 bytes when transmitted
 *               over the binary protocol.
 * - `payload` — Arbitrary bytes (e.g. JSON, Protobuf, plain text). Maximum 16 MB.
 */
struct Message {
    std::string topic;   ///< Routing key (exact match or `"#"` wildcard)
    std::string payload; ///< Message body — arbitrary bytes up to 16 MB
};

} // namespace metricmq