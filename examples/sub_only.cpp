// examples/sub_only.cpp - Subscriber-only test
#include "metricmq/pubsub.hpp"
#include <iostream>
#include <string>

int main() {
    std::cout << "=== Subscriber Only Demo ===\n";
    std::cout << "Listening on topic 'chat' (Ctrl+C to stop)...\n\n";

    metricmq::Subscriber sub("127.0.0.1", 6379);
    
    sub.subscribe("chat", [](const std::string& topic, const std::string& payload) {
        std::cout << "[RECEIVED] " << topic << ": " << payload << "\n";
    });

    return 0;
}
