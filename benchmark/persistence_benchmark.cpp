// benchmark/persistence_benchmark.cpp
// Persistence layer benchmarks (LMDB)

#include <benchmark/benchmark.h>
#include "../src/storage/LmdbStorage.hpp"
#include <random>

using namespace metricmq::storage;

// Benchmark: Sequential writes
static void BM_LMDB_SequentialWrite(benchmark::State& state) {
    LmdbStorage storage("bench_lmdb.db");
    uint64_t seq = 1;
    std::string topic = "bench/topic";
    std::string payload(state.range(0), 'x');
    
    for (auto _ : state) {
        storage.save(seq++, topic, payload);
    }
    
    state.SetItemsProcessed(state.iterations());
    state.SetBytesProcessed(state.iterations() * payload.size());
    
    // Cleanup
    std::remove("bench_lmdb.db");
    std::remove("bench_lmdb.db-lock");
}
BENCHMARK(BM_LMDB_SequentialWrite)
    ->Arg(100)
    ->Arg(1024)
    ->Arg(10240);

// Benchmark: Random reads
static void BM_LMDB_RandomRead(benchmark::State& state) {
    LmdbStorage storage("bench_lmdb_read.db");
    
    // Pre-populate database
    const int num_records = 1000;
    std::string payload = "test_payload";
    for (int i = 1; i <= num_records; i++) {
        storage.save(i, "topic", payload);
    }
    
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(1, num_records);
    
    for (auto _ : state) {
        uint64_t seq = dis(gen);
        auto result = storage.load_range(seq, seq);
        benchmark::DoNotOptimize(result);
    }
    
    state.SetItemsProcessed(state.iterations());
    
    // Cleanup
    std::remove("bench_lmdb_read.db");
    std::remove("bench_lmdb_read.db-lock");
}
BENCHMARK(BM_LMDB_RandomRead);

// Benchmark: Range scan
static void BM_LMDB_RangeScan(benchmark::State& state) {
    LmdbStorage storage("bench_lmdb_range.db");
    
    // Pre-populate
    const int num_records = 10000;
    std::string payload = "test";
    for (int i = 1; i <= num_records; i++) {
        storage.save(i, "topic", payload);
    }
    
    const int range_size = state.range(0);
    
    for (auto _ : state) {
        auto results = storage.load_range(1, range_size);
        benchmark::DoNotOptimize(results);
    }
    
    state.SetItemsProcessed(state.iterations() * range_size);
    
    // Cleanup
    std::remove("bench_lmdb_range.db");
    std::remove("bench_lmdb_range.db-lock");
}
BENCHMARK(BM_LMDB_RangeScan)
    ->Arg(10)
    ->Arg(100)
    ->Arg(1000);

// Benchmark: ACK persistence
static void BM_LMDB_ACK_Write(benchmark::State& state) {
    LmdbStorage storage("bench_lmdb_ack.db");
    std::string client_id = "bench_client";
    uint64_t seq = 1;
    
    for (auto _ : state) {
        storage.save_ack(client_id, seq++);
    }
    
    state.SetItemsProcessed(state.iterations());
    
    // Cleanup
    std::remove("bench_lmdb_ack.db");
    std::remove("bench_lmdb_ack.db-lock");
}
BENCHMARK(BM_LMDB_ACK_Write);

// Benchmark: ACK load
static void BM_LMDB_ACK_Load(benchmark::State& state) {
    LmdbStorage storage("bench_lmdb_ack_load.db");
    std::string client_id = "bench_client";
    
    // Pre-populate ACKs
    const int num_acks = state.range(0);
    for (int i = 1; i <= num_acks; i++) {
        storage.save_ack(client_id, i);
    }
    
    for (auto _ : state) {
        auto acks = storage.load_acks(client_id);
        benchmark::DoNotOptimize(acks);
    }
    
    state.SetItemsProcessed(state.iterations() * num_acks);
    
    // Cleanup
    std::remove("bench_lmdb_ack_load.db");
    std::remove("bench_lmdb_ack_load.db-lock");
}
BENCHMARK(BM_LMDB_ACK_Load)
    ->Arg(100)
    ->Arg(1000)
    ->Arg(10000);

BENCHMARK_MAIN();
