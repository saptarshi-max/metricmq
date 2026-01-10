// benchmark/latency_benchmark.cpp
// Comprehensive latency benchmarks for MetricMQ

#include <benchmark/benchmark.h>
#include "../include/metricmq/binary_pubsub.hpp"
#include <thread>
#include <chrono>

using namespace metricmq;

// Benchmark: Message publish latency (no subscribers)
static void BM_PublishLatency_NoSubscribers(benchmark::State& state) {
    BinaryPublisher pub("127.0.0.1", 6379);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    std::string payload(state.range(0), 'x');  // Variable payload size
    
    for (auto _ : state) {
        auto start = std::chrono::high_resolution_clock::now();
        pub.send("bench/test", payload);
        auto end = std::chrono::high_resolution_clock::now();
        
        auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
        state.SetIterationTime(elapsed.count() / 1e9);
    }
    
    state.SetItemsProcessed(state.iterations());
    state.SetBytesProcessed(state.iterations() * payload.size());
}
BENCHMARK(BM_PublishLatency_NoSubscribers)
    ->Arg(10)      // 10 bytes
    ->Arg(100)     // 100 bytes
    ->Arg(1024)    // 1 KB
    ->Arg(10240)   // 10 KB
    ->UseManualTime();

// Benchmark: End-to-end latency (publish → receive)
static void BM_EndToEndLatency(benchmark::State& state) {
    std::atomic<int> received_count{0};
    std::atomic<bool> ready{false};
    
    // Subscriber in separate thread
    std::thread sub_thread([&]() {
        BinarySubscriber sub("bench_subscriber", "127.0.0.1", 6379);
        sub.subscribe("bench/e2e", [&](const std::string& topic, const std::string& payload) {
            received_count++;
        });
        ready = true;
        
        while (received_count < state.max_iterations) {
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
    });
    
    // Wait for subscriber to be ready
    while (!ready) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    BinaryPublisher pub("127.0.0.1", 6379);
    std::string payload = "benchmark_message";
    
    for (auto _ : state) {
        auto start = std::chrono::high_resolution_clock::now();
        pub.send("bench/e2e", payload);
        
        // Wait for message to be received
        int initial = received_count.load();
        while (received_count.load() == initial) {
            std::this_thread::sleep_for(std::chrono::microseconds(1));
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
        state.SetIterationTime(elapsed.count() / 1e9);
    }
    
    sub_thread.join();
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_EndToEndLatency)->UseManualTime()->Iterations(100);

// Benchmark: ACK latency
static void BM_AckLatency(benchmark::State& state) {
    std::atomic<int> ack_count{0};
    
    BinarySubscriber sub("bench_ack", "127.0.0.1", 6379);
    sub.subscribe("bench/ack", [&](const std::string& topic, const std::string& payload) {
        ack_count++;
    });
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    BinaryPublisher pub("127.0.0.1", 6379);
    
    for (auto _ : state) {
        auto start = std::chrono::high_resolution_clock::now();
        pub.send("bench/ack", "test");
        
        // Wait for ACK
        int initial = ack_count.load();
        while (ack_count.load() == initial) {
            std::this_thread::sleep_for(std::chrono::microseconds(1));
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
        state.SetIterationTime(elapsed.count() / 1e9);
    }
    
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_AckLatency)->UseManualTime()->Iterations(100);

// Benchmark: Subscribe latency
static void BM_SubscribeLatency(benchmark::State& state) {
    for (auto _ : state) {
        auto start = std::chrono::high_resolution_clock::now();
        
        BinarySubscriber sub("bench_sub_" + std::to_string(state.iterations()), "127.0.0.1", 6379);
        sub.subscribe("bench/subscribe", [](const std::string& topic, const std::string& payload) {
            // Dummy callback
        });
        
        auto end = std::chrono::high_resolution_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
        state.SetIterationTime(elapsed.count() / 1e9);
    }
    
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_SubscribeLatency)->UseManualTime();

BENCHMARK_MAIN();
