// Exactly-Once Delivery Test
// Tests that messages are delivered exactly once even with disconnects/reconnects

#include <metricmq/binary_pubsub.hpp>
#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <vector>
#include <unordered_set>

using namespace metricmq;
using namespace std::chrono_literals;

void test1_basic_ack_flow() {
    std::cout << "\n=== Test 1: Basic ACK Flow ===\n";
    
    // Subscribe with client ID
    std::string client_id = "test-client-1";
    BinarySubscriber sub(client_id, "127.0.0.1", 6379);
    
    std::atomic<int> received_count{0};
    std::unordered_set<uint64_t> received_sequences;
    
    std::thread sub_thread([&]() {
        sub.subscribe("test/ack", [&](const std::string& topic, const std::string& payload) {
            received_count++;
            std::cout << "Received: " << payload << "\n";
        }, true);  // auto_ack = true
    });
    
    // Wait for subscription
    std::this_thread::sleep_for(500ms);
    
    // Publish 10 messages
    BinaryPublisher pub("127.0.0.1", 6379);
    for (int i = 0; i < 10; i++) {
        pub.send("test/ack", "Message " + std::to_string(i));
        std::this_thread::sleep_for(100ms);
    }
    
    // Wait for delivery
    std::this_thread::sleep_for(1s);
    
    std::cout << "Test 1 Result: Received " << received_count << " messages (expected 10)\n";
    
    if (received_count == 10) {
        std::cout << "✅ PASSED: All messages received\n";
    } else {
        std::cout << "❌ FAILED: Expected 10, got " << received_count << "\n";
    }
    
    sub_thread.detach();
}

void test2_no_duplicates_on_reconnect() {
    std::cout << "\n=== Test 2: No Duplicates on Reconnect ===\n";
    
    // Publish 50 messages BEFORE subscriber connects
    BinaryPublisher pub("127.0.0.1", 6379);
    std::cout << "Publishing 50 messages...\n";
    for (int i = 0; i < 50; i++) {
        pub.send("test/reconnect", "Msg-" + std::to_string(i));
    }
    
    std::this_thread::sleep_for(500ms);
    
    // First connection: receive 25 messages, then disconnect
    std::cout << "\nFirst connection (receive 25)...\n";
    {
        std::string client_id = "test-client-2";
        BinarySubscriber sub(client_id, "127.0.0.1", 6379);
        std::atomic<int> count{0};
        
        std::thread sub_thread([&]() {
            sub.subscribe("test/reconnect", [&](const std::string& topic, const std::string& payload) {
                count++;
                std::cout << "  [1st connection] " << payload << "\n";
                
                if (count == 25) {
                    std::cout << "Disconnecting after 25 messages...\n";
                    std::this_thread::sleep_for(100ms);
                    // Thread will terminate, closing connection
                }
            });
        });
        
        // Wait until 25 received
        while (count < 25) {
            std::this_thread::sleep_for(100ms);
        }
        
        sub_thread.join();
    }  // Subscriber destroyed, connection closed
    
    std::this_thread::sleep_for(1s);
    
    // Second connection: should receive ONLY remaining 25 messages (no duplicates)
    std::cout << "\nSecond connection (should receive remaining 25)...\n";
    {
        std::string client_id = "test-client-2";  // Same client ID!
        BinarySubscriber sub2(client_id, "127.0.0.1", 6379);
        std::atomic<int> count2{0};
        
        std::thread sub_thread2([&]() {
            sub2.subscribe("test/reconnect", [&](const std::string& topic, const std::string& payload) {
                count2++;
                std::cout << "  [2nd connection] " << payload << "\n";
            });
        });
        
        std::this_thread::sleep_for(3s);
        
        std::cout << "Test 2 Result: Second connection received " << count2 << " messages (expected 25)\n";
        
        if (count2 == 25) {
            std::cout << "✅ PASSED: No duplicates, received exactly remaining 25\n";
        } else {
            std::cout << "❌ FAILED: Expected 25, got " << count2 << " (duplicates!)\n";
        }
        
        sub_thread2.detach();
    }
}

void test3_multiple_clients() {
    std::cout << "\n=== Test 3: Multiple Clients with Independent ACK Tracking ===\n";
    
    // Two clients with different IDs
    std::string id_A = "client-A";
    std::string id_B = "client-B";
    BinarySubscriber sub1(id_A, "127.0.0.1", 6379);
    BinarySubscriber sub2(id_B, "127.0.0.1", 6379);
    
    std::atomic<int> count_A{0};
    std::atomic<int> count_B{0};
    
    std::thread thread_A([&]() {
        sub1.subscribe("test/multi", [&](const std::string& topic, const std::string& payload) {
            count_A++;
            std::cout << "  Client A: " << payload << "\n";
        });
    });
    
    std::thread thread_B([&]() {
        sub2.subscribe("test/multi", [&](const std::string& topic, const std::string& payload) {
            count_B++;
            std::cout << "  Client B: " << payload << "\n";
        });
    });
    
    std::this_thread::sleep_for(500ms);
    
    // Publish 20 messages
    BinaryPublisher pub("127.0.0.1", 6379);
    for (int i = 0; i < 20; i++) {
        pub.send("test/multi", "Multi-" + std::to_string(i));
        std::this_thread::sleep_for(50ms);
    }
    
    std::this_thread::sleep_for(2s);
    
    std::cout << "Test 3 Result:\n";
    std::cout << "  Client A received: " << count_A << " (expected 20)\n";
    std::cout << "  Client B received: " << count_B << " (expected 20)\n";
    
    if (count_A == 20 && count_B == 20) {
        std::cout << "✅ PASSED: Both clients received all messages\n";
    } else {
        std::cout << "❌ FAILED\n";
    }
    
    thread_A.detach();
    thread_B.detach();
}

void test4_sequential_ack_tracking() {
    std::cout << "\n=== Test 4: Sequential ACK Tracking ===\n";
    
    // Publish 100 messages
    BinaryPublisher pub("127.0.0.1", 6379);
    std::cout << "Publishing 100 messages...\n";
    for (int i = 0; i < 100; i++) {
        pub.send("test/sequential", "Seq-" + std::to_string(i));
    }
    
    std::this_thread::sleep_for(500ms);
    
    // Subscribe and receive all
    std::string client_id = "test-client-seq";
    BinarySubscriber sub(client_id, "127.0.0.1", 6379);
    std::atomic<int> count{0};
    
    std::thread sub_thread([&]() {
        sub.subscribe("test/sequential", [&](const std::string& topic, const std::string& payload) {
            count++;
            // All messages auto-ACK'd
        });
    });
    
    std::this_thread::sleep_for(3s);
    
    std::cout << "Test 4 Result: Received " << count << " messages (expected 100)\n";
    
    if (count == 100) {
        std::cout << "✅ PASSED: All messages delivered and ACK'd\n";
    } else {
        std::cout << "❌ FAILED: Expected 100, got " << count << "\n";
    }
    
    sub_thread.detach();
}

void test5_wildcard_with_ack() {
    std::cout << "\n=== Test 5: Wildcard Subscription with ACK ===\n";
    
    std::string client_id = "test-client-wildcard";
    BinarySubscriber sub(client_id, "127.0.0.1", 6379);
    std::atomic<int> count{0};
    
    std::thread sub_thread([&]() {
        sub.subscribe("#", [&](const std::string& topic, const std::string& payload) {
            count++;
            std::cout << "  Wildcard: " << topic << " -> " << payload << "\n";
        });
    });
    
    std::this_thread::sleep_for(500ms);
    
    // Publish to multiple topics
    BinaryPublisher pub("127.0.0.1", 6379);
    pub.send("sensors/temp", "25.5");
    pub.send("sensors/humidity", "60%");
    pub.send("alerts/critical", "High temp!");
    pub.send("status/ok", "All good");
    
    std::this_thread::sleep_for(2s);
    
    std::cout << "Test 5 Result: Received " << count << " messages (expected 4)\n";
    
    if (count == 4) {
        std::cout << "✅ PASSED: Wildcard subscription works with ACK\n";
    } else {
        std::cout << "❌ FAILED: Expected 4, got " << count << "\n";
    }
    
    sub_thread.detach();
}

int main() {
    std::cout << "╔════════════════════════════════════════════╗\n";
    std::cout << "║   MetricMQ Exactly-Once Delivery Tests   ║\n";
    std::cout << "╚════════════════════════════════════════════╝\n";
    
    std::cout << "\n⚠️  Make sure broker is running: ./metricmq-broker.exe\n";
    std::cout << "Press Enter to start tests...";
    std::cin.get();
    
    try {
        test1_basic_ack_flow();
        std::this_thread::sleep_for(2s);
        
        test2_no_duplicates_on_reconnect();
        std::this_thread::sleep_for(2s);
        
        test3_multiple_clients();
        std::this_thread::sleep_for(2s);
        
        test4_sequential_ack_tracking();
        std::this_thread::sleep_for(2s);
        
        test5_wildcard_with_ack();
        
        std::cout << "\n╔════════════════════════════════════════════╗\n";
        std::cout << "║          All Tests Completed!             ║\n";
        std::cout << "╚════════════════════════════════════════════╝\n";
        
    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << "\n";
        return 1;
    }
    
    std::cout << "\nPress Enter to exit...";
    std::cin.get();
    
    return 0;
}
