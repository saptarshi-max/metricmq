// include/metricmq/logger.hpp
#pragma once
#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <memory>

namespace metricmq {

class Logger {
public:
    static void init(const std::string& log_file = "logs/metricmq.log", 
                     spdlog::level::level_enum level = spdlog::level::info);
    
    static std::shared_ptr<spdlog::logger>& get();
    
private:
    static std::shared_ptr<spdlog::logger> logger_;
};

// Convenience macros
#define LOG_TRACE(...)    metricmq::Logger::get()->trace(__VA_ARGS__)
#define LOG_DEBUG(...)    metricmq::Logger::get()->debug(__VA_ARGS__)
#define LOG_INFO(...)     metricmq::Logger::get()->info(__VA_ARGS__)
#define LOG_WARN(...)     metricmq::Logger::get()->warn(__VA_ARGS__)
#define LOG_ERROR(...)    metricmq::Logger::get()->error(__VA_ARGS__)
#define LOG_CRITICAL(...) metricmq::Logger::get()->critical(__VA_ARGS__)

} // namespace metricmq
