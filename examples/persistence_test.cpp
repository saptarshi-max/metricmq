// examples/persistence_test.cpp
#include "metricmq/pubsub.hpp"
#include <iostream>
#include <thread>
#include <chrono>

using namespace metricmq;

int main() {
    std::cout << "=== Persistence Test ===\n";
    std::cout << "1. Publishing 5 messages to 'test/persistence' topic\n";
    
    Publisher pub("localhost", 6379);
    
    // Publish 5 messages
    for (int i = 1; i <= 5; ++i) {
        std::string msg = "Message " + std::to_string(i);
        pub.send("test/persistence", msg);
        std::cout << "   Published: " << msg << "\n";
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    std::cout << "\n2. Subscribing to 'test/persistence' topic\n";
    std::cout << "   Expected: Should receive all 5 messages (replayed from persistence)\n";
    
    Subscriber sub("localhost", 6379);
    int count = 0;
    
    sub.subscribe("test/persistence", [&](const std::string& topic, const std::string& payload) {
        ++count;
        std::cout << "   Received [" << count << "]: " << payload << "\n";
        
        if (count >= 5) {
            std::cout << "\n✅ SUCCESS: Received all 5 messages from persistence!\n";
            exit(0);
        }
    });
    
    // Wait for messages
    std::this_thread::sleep_for(std::chrono::seconds(3));
    
    if (count < 5) {
        std::cout << "\n❌ FAILED: Only received " << count << " out of 5 messages\n";
        return 1;
    }
    
    return 0;
}
