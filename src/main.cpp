// src/main.cpp
#include "metricmq/broker.hpp"
#include "metricmq/client.hpp"
#include <iostream>
#include <thread>
#include <chrono>

int main() {
    std::cout << "=== MetricMQ Starting ===\n\n";

    // Start broker in background thread
    metricmq::Broker broker(6379);
    std::thread broker_thread([&broker] { broker.run(); });

    std::this_thread::sleep_for(std::chrono::milliseconds(500)); // give broker time to start

    // Publisher example
    metricmq::Publisher pub("127.0.0.1", 6379);
    std::cout << "Publisher sending 5 messages...\n";
    for (int i = 1; i <= 5; ++i) {
        std::string msg = "Hello from publisher #" + std::to_string(i);
        pub.send("demo", msg);
        std::cout << "Sent: " << msg << "\n";
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    // Subscriber example (in main thread for simplicity)
    std::cout << "\nStarting subscriber (press Ctrl+C to stop)...\n";
    metricmq::Subscriber sub("127.0.0.1", 6379);
    sub.subscribe("demo", [](const std::string& topic, const std::string& payload) {
        (void)topic; // topic available if needed
        std::cout << "Received: " << payload << "\n";
    });
    sub.run();  // blocking loop

    broker_thread.join();
    return 0;
}