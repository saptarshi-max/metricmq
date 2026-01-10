// Queue Producer (PUSH) Example
#include "metricmq/queue.hpp"
#include <iostream>
#include <thread>
#include <chrono>

int main() {
    std::cout << "=== Queue Producer (PUSH) ===\n";
    std::cout << "Pushing tasks to queue 'tasks'...\n\n";

    metricmq::QueueProducer producer("127.0.0.1", 6379);
    
    for (int i = 1; i <= 10; ++i) {
        std::string task = "task_" + std::to_string(i);
        producer.push("tasks", task);
        std::cout << "[PUSHED] tasks: " << task << "\n";
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    std::cout << "\nDone! Press Enter to exit...\n";
    std::cin.get();
    return 0;
}
