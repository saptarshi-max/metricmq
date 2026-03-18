/**
 * @file types.hpp
 * @brief Fundamental type aliases used throughout the MetricMQ API.
 *
 * Named aliases make function signatures self-documenting and allow future
 * migration to custom string types without changing call sites.
 */
#pragma once
#include <string>

namespace metricmq {

using Topic   = std::string; ///< UTF-8 topic string (max 256 bytes over the wire).
using Payload = std::string; ///< Arbitrary message body (max 16 MB over the wire).

} // namespace metricmq
