// Protocol Benchmark: RESP vs Binary
// Compares latency, throughput, and message size
#include "metricmq/pubsub.hpp"
#include "metricmq/binary_pubsub.hpp"
#include <iostream>
#include <chrono>
#include <vector>
#include <numeric>
#include <thread>

namespace metricmq {
    // Forward declarations to access internals
    class RespParser;
    class RespValue;
    class BinaryProtocol;
}

using namespace std::chrono;

struct BenchmarkResult {
    std::string protocol_name;
    size_t message_count;
    size_t payload_size;
    double avg_latency_us;
    double throughput_msg_per_sec;
    size_t wire_size_bytes;
};

void printResults(const std::vector<BenchmarkResult>& results) {
    std::cout << "\n=== Protocol Benchmark Results ===\n\n";
    std::cout << "Protocol     | Msgs  | Payload | Latency (μs) | Throughput (msg/s) | Wire Size (bytes)\n";
    std::cout << "------------ | ----- | ------- | ------------ | ------------------ | -----------------\n";
    
    for (const auto& r : results) {
        printf("%-12s | %5zu | %4zuB   | %12.2f | %18.0f | %zu\n",
               r.protocol_name.c_str(),
               r.message_count,
               r.payload_size,
               r.avg_latency_us,
               r.throughput_msg_per_sec,
               r.wire_size_bytes);
    }
    
    std::cout << "\n";
}

size_t calculateRespSize(const std::string& topic, const std::string& payload) {
    // PUBLISH command: *3\r\n$7\r\nPUBLISH\r\n$<topic_len>\r\n<topic>\r\n$<payload_len>\r\n<payload>\r\n
    // Approximate: *3\r\n + $7\r\nPUBLISH\r\n + $<tlen>\r\n<topic>\r\n + $<plen>\r\n<payload>\r\n
    size_t header = 4; // *3\r\n
    header += 13; // $7\r\nPUBLISH\r\n
    header += 1 + std::to_string(topic.size()).size() + 2 + topic.size() + 2; // $len\r\ntopic\r\n
    header += 1 + std::to_string(payload.size()).size() + 2 + payload.size() + 2; // $len\r\npayload\r\n
    return header;
}

size_t calculateBinarySize(const std::string& topic, const std::string& payload) {
    // Binary: 16-byte header + topic + payload
    return 16 + topic.size() + payload.size();
}

BenchmarkResult benchmarkMessageSize(const std::string& protocol, size_t payload_size) {
    std::string topic = "benchmark";
    std::string payload(payload_size, 'X');
    
    size_t wire_size = 0;
    if (protocol == "RESP") {
        wire_size = calculateRespSize(topic, payload);
    } else {
        wire_size = calculateBinarySize(topic, payload);
    }
    
    return {protocol, 1, payload_size, 0, 0, wire_size};
}

BenchmarkResult benchmarkThroughput(const std::string& protocol, size_t num_messages, size_t payload_size) {
    std::string topic = "benchmark";
    std::string payload(payload_size, 'X');
    
    std::cout << "Benchmarking " << protocol << " throughput (" << num_messages << " messages, "
              << payload_size << "B payload)...\n";
    
    auto start = high_resolution_clock::now();
    
    if (protocol == "RESP") {
        metricmq::Publisher pub("127.0.0.1", 6379);
        for (size_t i = 0; i < num_messages; ++i) {
            pub.send(topic, payload);
        }
    } else {
        metricmq::BinaryPublisher pub("127.0.0.1", 6379);
        for (size_t i = 0; i < num_messages; ++i) {
            pub.send(topic, payload);
        }
    }
    
    auto end = high_resolution_clock::now();
    auto duration_us = duration_cast<microseconds>(end - start).count();
    
    double avg_latency = static_cast<double>(duration_us) / num_messages;
    double throughput = (num_messages * 1000000.0) / duration_us;
    
    size_t wire_size = protocol == "RESP" ? 
        calculateRespSize(topic, payload) : 
        calculateBinarySize(topic, payload);
    
    return {protocol, num_messages, payload_size, avg_latency, throughput, wire_size};
}

int main() {
    std::cout << "=== MetricMQ Protocol Benchmark ===\n";
    std::cout << "Comparing RESP vs Binary Protocol\n\n";
    
    std::vector<BenchmarkResult> results;
    
    // 1. Message Size Comparison
    std::cout << "--- Message Size Analysis ---\n";
    std::vector<size_t> payload_sizes = {0, 8, 16, 32, 64, 128, 256, 512, 1024};
    
    for (size_t size : payload_sizes) {
        auto resp_size = benchmarkMessageSize("RESP", size);
        auto binary_size = benchmarkMessageSize("Binary", size);
        
        double overhead_percent = ((double)resp_size.wire_size_bytes / binary_size.wire_size_bytes - 1.0) * 100;
        
        std::cout << "Payload: " << size << "B -> "
                  << "RESP: " << resp_size.wire_size_bytes << "B, "
                  << "Binary: " << binary_size.wire_size_bytes << "B "
                  << "(+" << overhead_percent << "% overhead)\n";
    }
    
    std::cout << "\n--- Throughput Benchmarks ---\n";
    std::cout << "NOTE: Broker must be running on port 6379\n";
    std::cout << "Press Enter to start benchmarks (or Ctrl+C to skip)...\n";
    std::cin.get();
    
    // Wait for broker
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // 2. Throughput Comparison (64B messages)
    try {
        results.push_back(benchmarkThroughput("RESP", 10000, 64));
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        results.push_back(benchmarkThroughput("Binary", 10000, 64));
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        // 3. Throughput Comparison (1KB messages)
        results.push_back(benchmarkThroughput("RESP", 1000, 1024));
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        results.push_back(benchmarkThroughput("Binary", 1000, 1024));
        
        printResults(results);
        
        // Summary
        auto resp_64 = results[0];
        auto binary_64 = results[1];
        double speedup = binary_64.throughput_msg_per_sec / resp_64.throughput_msg_per_sec;
        
        std::cout << "=== Summary ===\n";
        std::cout << "Binary protocol is " << speedup << "x faster for 64B messages\n";
        std::cout << "Binary protocol reduces wire size by ~" 
                  << (1.0 - (double)binary_64.wire_size_bytes / resp_64.wire_size_bytes) * 100 
                  << "%\n";
        
    } catch (const std::exception& e) {
        std::cerr << "Benchmark failed: " << e.what() << "\n";
        std::cerr << "Make sure broker is running: .\\metricmq-broker.exe\n";
        return 1;
    }
    
    return 0;
}
