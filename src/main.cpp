// src/main.cpp
#include "broker.hpp"
#include "metricmq/logger.hpp"
#include "metricmq/metrics_server.hpp"
#include "metricmq/crypto.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <csignal>

// Global broker pointer for signal handler
std::atomic<metricmq::Broker*> g_broker(nullptr);
std::atomic<bool> g_shutdown_requested(false);

void signal_handler(int signal) {
    const char* signal_name = (signal == SIGINT) ? "SIGINT" : 
                              (signal == SIGTERM) ? "SIGTERM" : "UNKNOWN";
    
    std::cout << "\n\n📛 Received signal " << signal_name << " (" << signal << ")\n";
    
    if (g_shutdown_requested) {
        std::cout << "⚠️  Force quit (second signal)\n";
        std::exit(1);
    }
    
    g_shutdown_requested = true;
    
    // Trigger graceful shutdown
    auto* broker = g_broker.load();
    if (broker) {
        broker->stop();
    }
}

int main() {
    // Initialize logger first
    metricmq::Logger::init("logs/metricmq.log", spdlog::level::debug);
    
    // Initialize crypto subsystem (libsodium)
    if (!metricmq::crypto::init()) {
        std::cerr << "Failed to initialize crypto subsystem\n";
        return 1;
    }
    
    std::cout << "╔════════════════════════════════════════════╗\n";
    std::cout << "║         MetricMQ Broker v1.0              ║\n";
    std::cout << "║    Lightweight Message Queue for IoT      ║\n";
    std::cout << "╚════════════════════════════════════════════╝\n\n";

    // Install signal handlers
    std::signal(SIGINT, signal_handler);   // Ctrl+C
    std::signal(SIGTERM, signal_handler);  // Termination request
    
    std::cout << "📡 Starting broker on port 6379...\n";
    std::cout << "� Starting metrics server on port 9091...\n";
    std::cout << "💡 Press Ctrl+C for graceful shutdown\n\n";

    // Start metrics server
    metricmq::MetricsServer metrics_server(9091);
    try {
        metrics_server.start();
    } catch (const std::exception& ex) {
        std::cerr << "⚠️  Failed to start metrics server: " << ex.what() << "\n";
        std::cerr << "   Continuing without metrics endpoint...\n";
    }
    
    // Start broker
    metricmq::Broker broker(6379);
    g_broker.store(&broker);
    
    // Run broker (blocks until shutdown)
    broker.run();
    
    // Stop metrics server
    metrics_server.stop();
    
    std::cout << "\n👋 MetricMQ broker stopped\n";
    return 0;
}