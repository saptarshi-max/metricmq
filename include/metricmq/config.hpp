/**
 * @file config.hpp
 * @brief Broker startup configuration with sensible defaults.
 */
#pragma once

namespace metricmq {

/**
 * @brief Startup parameters for the MetricMQ broker.
 */
struct Config {
    int  port = 6379;         ///< TCP port to listen on. Default 6379 matches Redis.
    bool persistence = true;  ///< Enable LMDB persistence — messages survive restarts.
};

} // namespace metricmq
