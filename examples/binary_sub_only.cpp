// Binary Protocol Subscriber Example
#include "metricmq/binary_pubsub.hpp"
#include <iostream>

int main() {
    std::cout << "=== Binary Protocol Subscriber ===\n";
    std::cout << "Listening on topic 'chat' (Ctrl+C to stop)...\n\n";

    metricmq::BinarySubscriber sub("127.0.0.1", 6379);
    
    sub.subscribe("chat", [](const std::string& topic, const std::string& payload) {
        std::cout << "[RECEIVED] " << topic << ": " << payload << "\n";
    });

    return 0;
}
