// src/logger.cpp
#include "metricmq/logger.hpp"
#include <filesystem>
#include <iostream>

namespace metricmq {

std::shared_ptr<spdlog::logger> Logger::logger_;

void Logger::init(const std::string& log_file, spdlog::level::level_enum level) {
    try {
        // Create logs directory if it doesn't exist
        std::filesystem::path log_path(log_file);
        if (log_path.has_parent_path()) {
            std::filesystem::create_directories(log_path.parent_path());
        }
        
        // Create sinks
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        console_sink->set_level(spdlog::level::info);
        console_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");
        
        auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            log_file, 
            1024 * 1024 * 10,  // 10 MB max file size
            3                   // 3 rotating files
        );
        file_sink->set_level(spdlog::level::trace);
        file_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [thread %t] %v");
        
        // Create logger with both sinks
        std::vector<spdlog::sink_ptr> sinks{console_sink, file_sink};
        logger_ = std::make_shared<spdlog::logger>("metricmq", sinks.begin(), sinks.end());
        
        logger_->set_level(level);
        logger_->flush_on(spdlog::level::warn);  // Auto-flush on warnings and above
        
        spdlog::register_logger(logger_);
        
        logger_->info("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
        logger_->info("MetricMQ Logger Initialized");
        logger_->info("Log file: {}", log_file);
        logger_->info("Log level: {}", spdlog::level::to_string_view(level));
        logger_->info("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
        
    } catch (const spdlog::spdlog_ex& ex) {
        std::cerr << "Log initialization failed: " << ex.what() << std::endl;
    }
}

std::shared_ptr<spdlog::logger>& Logger::get() {
    if (!logger_) {
        // Auto-initialize with defaults if not explicitly initialized
        init();
    }
    return logger_;
}

} // namespace metricmq
