// examples/pubsub_raw_tcp.cpp - Full PUB/SUB demo
#include "metricmq/pubsub.hpp"
#include <iostream>
#include <string>
#include <thread>
#include <chrono>

int main() {
    std::cout << "PUB/SUB Raw TCP Demo\n\n";

    // Subscriber in background thread
    metricmq::Subscriber sub("127.0.0.1", 6379);
    std::thread sub_thread([&sub] {
        sub.subscribe("test", [](const std::string& topic, const std::string& payload) {
            std::cout << "Received [" << topic << "]: " << payload << "\n";
        });
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(500)); // wait for connect

    // Publisher
    metricmq::Publisher pub("127.0.0.1", 6379);
    for (int i = 1; i <= 10; ++i) {
        std::string msg = "Hello #" + std::to_string(i);
        pub.send("test", msg);
        std::cout << "Sent: " << msg << "\n";
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }

    std::cout << "\nPress Enter to stop...\n";
    std::cin.get();

    return 0;
}