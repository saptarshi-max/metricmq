/**
 * @file logger.hpp
 * @brief Structured logging singleton and convenience macros.
 *
 * Wraps spdlog with two sinks:
 * - **Colored stdout** — INFO level and above.
 * - **Rotating file** (`logs/metricmq.log`) — DEBUG level and above,
 *   max 5 MB per file, up to 3 rotated files.
 *
 * @par Initialization
 * Call `Logger::init()` once at startup. All `LOG_*` macros call `Logger::get()`
 * which returns a valid (but possibly default) logger even before `init()`.
 *
 * @par Usage
 * @code
 * LOG_INFO("Client connected: fd={} ip={}", fd, ip_str);
 * LOG_WARN("Buffer near limit: size={}", buf.size());
 * LOG_ERROR("Parse failed: {}", err.what());
 * @endcode
 *
 * Format strings follow the `{fmt}` library syntax (positional `{}` or named `{name}`).
 */
// include/metricmq/logger.hpp
#pragma once
#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <memory>

namespace metricmq {

/**
 * @brief Singleton wrapping a multi-sink spdlog logger.
 *
 * @note Never construct this class directly. Use the `LOG_*` macros.
 */
class Logger {
public:
    /**
     * @brief Initialize the logger with file and console sinks.
     *
     * Safe to call multiple times — subsequent calls are ignored.
     *
     * @param log_file Path to the rotating log file (default: `"logs/metricmq.log"`).
     *                 The directory is created if it does not exist.
     * @param level    Minimum log level for the file sink (default: `info`).
     */
    static void init(const std::string& log_file = "logs/metricmq.log",
                     spdlog::level::level_enum level = spdlog::level::info);

    /**
     * @brief Return the underlying spdlog logger.
     *
     * Returns a valid logger even before `init()` is called (using spdlog's default).
     * The `LOG_*` macros call this internally.
     */
    static std::shared_ptr<spdlog::logger>& get();

private:
    static std::shared_ptr<spdlog::logger> logger_;
};

/// @name Logging macros
/// Format strings use `{fmt}` syntax: `LOG_INFO("x={} y={}", x, y)`.
/// @{
#define LOG_TRACE(...)    metricmq::Logger::get()->trace(__VA_ARGS__)    ///< Very verbose tracing
#define LOG_DEBUG(...)    metricmq::Logger::get()->debug(__VA_ARGS__)    ///< Debug-level detail
#define LOG_INFO(...)     metricmq::Logger::get()->info(__VA_ARGS__)     ///< Normal operational events
#define LOG_WARN(...)     metricmq::Logger::get()->warn(__VA_ARGS__)     ///< Unexpected but non-fatal
#define LOG_ERROR(...)    metricmq::Logger::get()->error(__VA_ARGS__)    ///< Recoverable errors
#define LOG_CRITICAL(...) metricmq::Logger::get()->critical(__VA_ARGS__) ///< Fatal — broker will likely stop
/// @}

} // namespace metricmq
