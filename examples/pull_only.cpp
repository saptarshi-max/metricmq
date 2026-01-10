// Queue Consumer (PULL) Example
#include "metricmq/queue.hpp"
#include <iostream>

int main() {
    std::cout << "=== Queue Consumer (PULL) ===\n";
    std::cout << "Pulling tasks from queue 'tasks' (Ctrl+C to stop)...\n\n";

    metricmq::QueueConsumer consumer("127.0.0.1", 6379);
    
    consumer.pull("tasks", [](const std::string& payload) {
        std::cout << "[PULLED] " << payload << "\n";
    });

    return 0;
}
