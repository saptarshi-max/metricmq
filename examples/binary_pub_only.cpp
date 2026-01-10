// Binary Protocol Publisher Example
#include "metricmq/binary_pubsub.hpp"
#include <iostream>
#include <thread>
#include <chrono>

int main() {
    std::cout << "=== Binary Protocol Publisher ===\n";
    std::cout << "Sending messages on topic 'chat'...\n\n";

    metricmq::BinaryPublisher pub("127.0.0.1", 6379);
    
    for (int i = 1; i <= 10; ++i) {
        std::string msg = "Binary Message #" + std::to_string(i);
        pub.send("chat", msg);
        std::cout << "[SENT] chat: " << msg << "\n";
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    std::cout << "\nDone! Press Enter to exit...\n";
    std::cin.get();
    return 0;
}
