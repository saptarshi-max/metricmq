// benchmark/throughput.cpp
// Simple standalone throughput test — no Google Benchmark dependency.
// Requires a running broker on 127.0.0.1:6379.
//
// Usage:
//   ./Release/throughput.exe [message_count] [payload_bytes]
//   ./Release/throughput.exe 100000 512
//
// Reports: total time, messages/sec, MB/sec, average latency per message.

#include "metricmq/pubsub.hpp"
#include <iostream>
#include <string>
#include <chrono>
#include <stdexcept>

static constexpr uint64_t DEFAULT_COUNT   = 100'000;
static constexpr size_t   DEFAULT_PAYLOAD = 256;

int main(int argc, char* argv[]) {
    uint64_t count   = DEFAULT_COUNT;
    size_t   payload = DEFAULT_PAYLOAD;

    if (argc >= 2) {
        try { count = std::stoull(argv[1]); } catch (...) {}
    }
    if (argc >= 3) {
        try { payload = std::stoul(argv[2]); } catch (...) {}
    }

    std::string topic = "bench/throughput";
    std::string msg(payload, 'x');

    std::cout << "MetricMQ Throughput Benchmark\n";
    std::cout << "  Messages : " << count   << "\n";
    std::cout << "  Payload  : " << payload << " bytes\n";
    std::cout << "  Topic    : " << topic   << "\n\n";

    metricmq::Publisher pub("127.0.0.1", 6379);

    // Warm-up: 1000 messages, not timed
    for (int i = 0; i < 1000; ++i) pub.send(topic, msg);

    auto t0 = std::chrono::steady_clock::now();
    for (uint64_t i = 0; i < count; ++i) {
        pub.send(topic, msg);
    }
    auto t1 = std::chrono::steady_clock::now();

    double elapsed_s = std::chrono::duration<double>(t1 - t0).count();
    double msg_per_s = static_cast<double>(count) / elapsed_s;
    double mb_per_s  = (static_cast<double>(count) * payload) / elapsed_s / (1024.0 * 1024.0);
    double us_per_msg = (elapsed_s * 1e6) / static_cast<double>(count);

    std::cout << "Results:\n";
    std::cout << "  Elapsed       : " << elapsed_s   << " s\n";
    std::cout << "  Throughput    : " << static_cast<uint64_t>(msg_per_s) << " msg/s\n";
    std::cout << "  Bandwidth     : " << mb_per_s    << " MB/s\n";
    std::cout << "  Avg latency   : " << us_per_msg  << " us/msg\n";

    return 0;
}
