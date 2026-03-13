// benchmark/throughput_benchmark.cpp
// Throughput benchmarks for MetricMQ

#include <benchmark/benchmark.h>
#include "../include/metricmq/binary_pubsub.hpp"
#include <thread>
#include <atomic>
#include <vector>

using namespace metricmq;

// Benchmark: Single publisher throughput
static void BM_SinglePublisher_Throughput(benchmark::State& state) {
    BinaryPublisher pub("127.0.0.1", 6379);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    std::string payload(state.range(0), 'x');
    int64_t total_bytes = 0;
    
    for (auto _ : state) {
        pub.send("bench/throughput", payload);
        total_bytes += payload.size();
    }
    
    state.SetItemsProcessed(state.iterations());
    state.SetBytesProcessed(total_bytes);
}
BENCHMARK(BM_SinglePublisher_Throughput)
    ->Arg(10)
    ->Arg(100)
    ->Arg(1024)
    ->Arg(10240);

// Benchmark: Multiple concurrent publishers
static void BM_MultiPublisher_Throughput(benchmark::State& state) {
    const int num_publishers = state.range(0);
    std::vector<std::thread> threads;
    std::atomic<int64_t> total_published{0};
    std::atomic<bool> running{true};
    
    // Create publishers in threads
    for (int i = 0; i < num_publishers; i++) {
        threads.emplace_back([&, i]() {
            BinaryPublisher pub("127.0.0.1", 6379);
            std::string payload = "benchmark_message_" + std::to_string(i);
            
            while (running) {
                pub.send("bench/multi", payload);
                total_published++;
            }
        });
    }
    
    // Run for fixed duration
    std::this_thread::sleep_for(std::chrono::seconds(1));
    running = false;
    
    for (auto& t : threads) {
        t.join();
    }
    
    state.counters["publishers"] = num_publishers;
    state.counters["total_msgs"] = total_published.load();
    state.counters["msgs_per_sec"] = benchmark::Counter(
        total_published.load(), 
        benchmark::Counter::kIsRate
    );
}
BENCHMARK(BM_MultiPublisher_Throughput)
    ->Arg(1)
    ->Arg(2)
    ->Arg(4)
    ->Arg(8)
    ->Unit(benchmark::kSecond)
    ->Iterations(1);

// Benchmark: Single subscriber throughput
static void BM_SingleSubscriber_Throughput(benchmark::State& state) {
    std::atomic<int64_t> received_count{0};
    std::atomic<bool> running{true};
    
    // Subscriber thread
    std::thread sub_thread([&]() {
        BinarySubscriber sub("bench_subscriber", "127.0.0.1", 6379);
        sub.subscribe("bench/sub_throughput", [&](const std::string&, const std::string&) {
            received_count++;
        });
        
        while (running) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    });
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    BinaryPublisher pub("127.0.0.1", 6379);
    std::string payload = "test_message";
    
    for (auto _ : state) {
        pub.send("bench/sub_throughput", payload);
    }

    // Wait for all messages to be received
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    running = false;
    sub_thread.join();
    
    state.SetItemsProcessed(state.iterations());
    state.counters["received"] = received_count.load();
    state.counters["loss_rate"] = (state.iterations() - received_count.load()) / (double)state.iterations();
}
BENCHMARK(BM_SingleSubscriber_Throughput);

// Benchmark: Multiple subscribers (fan-out)
static void BM_FanOut_Throughput(benchmark::State& state) {
    const int num_subscribers = state.range(0);
    std::atomic<int64_t> total_received{0};
    std::atomic<bool> running{true};
    std::vector<std::thread> sub_threads;
    
    // Create subscribers
    for (int i = 0; i < num_subscribers; i++) {
        sub_threads.emplace_back([&, i]() {
            BinarySubscriber sub("bench_sub_" + std::to_string(i), "127.0.0.1", 6379);
            sub.subscribe("bench/fanout", [&](const std::string&, const std::string&) {
                total_received++;
            });
            
            while (running) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        });
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    BinaryPublisher pub("127.0.0.1", 6379);
    int64_t published = 0;
    
    for (auto _ : state) {
        pub.send("bench/fanout", "message");
        published++;
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    running = false;
    
    for (auto& t : sub_threads) {
        t.join();
    }
    
    state.SetItemsProcessed(state.iterations());
    state.counters["subscribers"] = num_subscribers;
    state.counters["expected_total"] = published * num_subscribers;
    state.counters["actual_total"] = total_received.load();
}
BENCHMARK(BM_FanOut_Throughput)
    ->Arg(1)
    ->Arg(5)
    ->Arg(10);

BENCHMARK_MAIN();
