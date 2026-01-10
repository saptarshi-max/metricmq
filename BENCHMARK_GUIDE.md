# Google Benchmark Integration for MetricMQ

## Overview
Complete benchmarking suite using Google Benchmark 1.8.3 to measure MetricMQ's performance across latency, throughput, protocol overhead, and persistence operations.

## Setup

### 1. Install Dependencies
```bash
cd build
conan install .. --build=missing
cmake ..
cmake --build . --config Release
```

### 2. Run Benchmarks
```bash
# All benchmarks
.\Release\latency_benchmark.exe
.\Release\throughput_benchmark.exe
.\Release\protocol_comparison_benchmark.exe
.\Release\persistence_benchmark.exe

# With JSON output
.\Release\latency_benchmark.exe --benchmark_out=latency.json --benchmark_out_format=json

# Specific filter
.\Release\throughput_benchmark.exe --benchmark_filter=Multi

# Custom options
.\Release\latency_benchmark.exe --benchmark_min_time=2.0 --benchmark_repetitions=5
```

## Benchmark Categories

### 1. Latency Benchmarks (`latency_benchmark.cpp`)

**BM_PublishLatency_NoSubscribers**
- Measures pure publish latency without subscribers
- Tests payload sizes: 10 bytes, 100 bytes, 1 KB, 10 KB
- Use case: Baseline overhead measurement

**BM_EndToEndLatency**
- Full publish → receive round-trip latency
- Includes network, broker processing, and delivery
- Use case: Real-world application latency

**BM_AckLatency**
- Exactly-once ACK round-trip time
- Measures persistence overhead
- Use case: Guaranteed delivery performance

**BM_SubscribeLatency**
- Connection + subscription setup time
- Use case: Client initialization overhead

### 2. Throughput Benchmarks (`throughput_benchmark.cpp`)

**BM_SinglePublisher_Throughput**
- Messages/second from one publisher
- Tests 10B, 100B, 1KB, 10KB payloads
- Metrics: msgs/sec, bytes/sec

**BM_MultiPublisher_Throughput**
- Concurrent publishers (1, 2, 4, 8 threads)
- Measures broker scalability
- Metrics: total throughput, msgs/publisher/sec

**BM_SingleSubscriber_Throughput**
- Delivery throughput to one subscriber
- Tracks message loss rate
- Use case: Consumer performance

**BM_FanOut_Throughput**
- Pub/sub pattern with multiple subscribers (1, 5, 10)
- Tests broker fan-out efficiency
- Metrics: expected vs actual delivery count

### 3. Protocol Benchmarks (`protocol_benchmark.cpp`)

**BM_BinaryProtocol_Encode/Decode**
- Binary protocol serialization performance
- Compared at 10B, 100B, 1KB payloads

**BM_RESPProtocol_Encode**
- RESP (Redis) protocol encoding for comparison
- Shows overhead difference

**BM_ProtocolOverhead_Binary**
- Measures header overhead at different payload sizes
- Metrics: overhead bytes, overhead percentage
- Expected: ~40% less than RESP

**BM_FrameParsing_Throughput**
- Batch parsing performance
- Use case: High-speed message processing

### 4. Persistence Benchmarks (`persistence_benchmark.cpp`)

**BM_LMDB_SequentialWrite**
- LMDB write throughput (100B, 1KB, 10KB)
- Use case: Message persistence rate

**BM_LMDB_RandomRead**
- Random access read performance
- Pre-populated with 1000 records

**BM_LMDB_RangeScan**
- Sequential scan performance (10, 100, 1000 records)
- Use case: Message replay

**BM_LMDB_ACK_Write/Load**
- ACK persistence overhead
- Tests 100-10K ACK entries
- Critical for exactly-once semantics

## Sample Output

```
Run on (8 X 3600 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x4)
  L1 Instruction 32 KiB (x4)
  L2 Unified 256 KiB (x4)
  L3 Unified 8192 KiB (x1)
------------------------------------------------------------------------
Benchmark                              Time             CPU   Iterations
------------------------------------------------------------------------
BM_PublishLatency_NoSubscribers/10   125 ns          120 ns      5600000
BM_PublishLatency_NoSubscribers/100  138 ns          135 ns      5200000
BM_PublishLatency_NoSubscribers/1024 245 ns          240 ns      2900000
BM_EndToEndLatency                   1.2 ms          0.8 ms          100
BM_AckLatency                        1.8 ms          1.1 ms          100
BM_SinglePublisher_Throughput/10   85000 items/s
BM_MultiPublisher_Throughput/4     340000 items/s (85k per publisher)
```

## Performance Targets

| Metric | Target | Actual (Example) |
|--------|--------|------------------|
| Publish Latency (1KB) | < 500 ns | ~245 ns |
| End-to-End Latency | < 2 ms | ~1.2 ms |
| Binary Protocol Overhead | < 20 bytes | ~15 bytes |
| Single Publisher Throughput | > 50K msgs/s | ~85K msgs/s |
| LMDB Write Throughput | > 100K writes/s | ~200K writes/s |

## Comparing Results

### Before/After Testing
```bash
# Baseline
.\Release\latency_benchmark.exe --benchmark_out=baseline.json --benchmark_out_format=json

# After changes
.\Release\latency_benchmark.exe --benchmark_out=optimized.json --benchmark_out_format=json

# Compare with Google's compare.py
python compare.py benchmarks baseline.json optimized.json
```

### CI/CD Integration
```yaml
# .github/workflows/benchmark.yml
- name: Run Benchmarks
  run: |
    .\Release\latency_benchmark.exe --benchmark_out=results.json --benchmark_out_format=json
    
- name: Store Results
  uses: benchmark-action/github-action-benchmark@v1
  with:
    tool: 'googlecpp'
    output-file-path: results.json
```

## Profiling Tips

### 1. CPU Profiling
```bash
# With VTune (Intel)
vtune -collect hotspots -- .\Release\latency_benchmark.exe

# With perf (Linux)
perf record -g .\Release\latency_benchmark
perf report
```

### 2. Memory Profiling
```bash
# With Valgrind/Massif
valgrind --tool=massif .\Release\persistence_benchmark.exe
ms_print massif.out.*
```

### 3. Analyzing Outliers
```bash
# Run with repetitions to get statistics
.\Release\latency_benchmark.exe --benchmark_repetitions=10 --benchmark_report_aggregates_only=false
```

## Custom Benchmark Creation

```cpp
#include <benchmark/benchmark.h>
#include "metricmq/binary_pubsub.hpp"

static void BM_MyCustomTest(benchmark::State& state) {
    // Setup
    BinaryPublisher pub("127.0.0.1", 6379);
    
    for (auto _ : state) {
        // Code to benchmark
        pub.publish("test", "payload");
    }
    
    // Metrics
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_MyCustomTest);

BENCHMARK_MAIN();
```

## Interpreting Results

### Latency Distribution
- **Time**: Wall-clock time (includes I/O waits)
- **CPU**: CPU time only
- **Iterations**: Auto-tuned for statistical significance

### Throughput Metrics
- **items/s**: Messages processed per second
- **bytes/s**: Data throughput
- **loss_rate**: Percentage of undelivered messages

### Custom Counters
```cpp
state.counters["overhead_percent"] = overhead / total * 100.0;
state.counters["msgs_per_sec"] = benchmark::Counter(
    total_messages, 
    benchmark::Counter::kIsRate
);
```

## Regression Testing

Store baseline results and compare on each build:
```bash
# Initial baseline
.\Release\latency_benchmark.exe --benchmark_out=baseline.json --benchmark_out_format=json

# After changes
.\Release\latency_benchmark.exe --benchmark_out=current.json --benchmark_out_format=json

# Assert no regression (>10% slowdown fails CI)
python scripts/check_regression.py baseline.json current.json --threshold=0.1
```

## Resources

- [Google Benchmark Documentation](https://github.com/google/benchmark)
- [MetricMQ Performance Tuning Guide](../PERFORMANCE.md)
- [Benchmarking Best Practices](https://github.com/google/benchmark/blob/main/docs/user_guide.md)
