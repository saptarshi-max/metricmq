<p align="center">
  <img src="assets/img/MetricMQ.png" alt="MetricMQ" width="500">
</p>

<p align="center">
  <a href="LICENSE"><img src="https://img.shields.io/badge/License-MIT-blue.svg" alt="License: MIT"></a>
  <a href="https://en.cppreference.com/w/cpp/20"><img src="https://img.shields.io/badge/C%2B%2B-20-blue.svg" alt="C++20"></a>
  <a href="https://github.com/yourusername/MetricMQ"><img src="https://img.shields.io/badge/platform-Windows%20%7C%20Linux%20%7C%20macOS-lightgrey.svg" alt="Platform"></a>
</p>

# MetricMQ

Lightweight message broker with pub/sub and queue patterns, binary protocol support, and LMDB persistence.

## Overview

MetricMQ is an embeddable message broker designed for IoT and edge computing environments. It provides pub/sub and queue messaging patterns with optional persistence.

**Key characteristics:**
- 328 KB binary size
- Embedded LMDB storage
- Dual protocol support (RESP + Binary)
- Runs on ESP32/ESP8266 and desktop platforms
- 106K msg/s throughput (measured, 10KB messages)

## Features

| Feature | Description |
|---------|-------------|
| Dual Protocol | RESP (Redis-compatible) + Binary Protocol |
| Protocol Detection | Automatic detection on first byte |
| Pub/Sub | Topic-based routing with wildcards |
| Queue Mode | PUSH/PULL with round-robin distribution |
| Exactly-Once Delivery | Sequence IDs with ACK tracking |
| Persistence | LMDB embedded database |
| Metrics | Prometheus endpoint on port 9091 |
| Platforms | Windows, Linux, macOS, ESP32, ESP8266 |

### Messaging Patterns

**Pub/Sub:** Multiple subscribers receive broadcast messages
```cpp
publisher.send("sensors/temp", "25.5°C");
subscriber1.subscribe("sensors/temp");  // receives message
subscriber2.subscribe("sensors/temp");  // receives message
```

**Queue (PUSH/PULL):** Round-robin distribution to consumers
```cpp
producer.push("jobs", "task-1");
producer.push("jobs", "task-2");
worker1.pull("jobs");  // gets task-1
worker2.pull("jobs");  // gets task-2
```

## Build Requirements

- Windows 10+ / Linux / macOS
- CMake 3.20+
- C++20 compiler (MSVC 2022, GCC 11+, Clang 13+)
- Conan 2.x package manager

## Installation

```bash
git clone https://github.com/yourusername/MetricMQ.git
cd MetricMQ
mkdir build && cd build
conan install .. --output-folder=. --build=missing
cmake .. -DCMAKE_TOOLCHAIN_FILE=conan_toolchain.cmake
cmake --build . --config Release
```

## Quick Test

Terminal 1:
```bash
cd build/Release
./metricmq-broker.exe
```

Terminal 2:
```bash
./sub_only.exe
```

Terminal 3:
```bash
./pub_only.exe
```

## Architecture

```
┌─────────────────────────────────────────────────┐
│              MetricMQ Broker                    │
│  Port 6379 (RESP + Binary Protocol)             │
│  Port 9091 (Prometheus Metrics)                 │
└───────────┬─────────────────────────────────────┘
            │
    ┌───────┴────────┐
    │                │
┌───▼────┐      ┌───▼────┐
│ RESP   │      │ Binary │
│Protocol│      │Protocol│
└───┬────┘      └───┬────┘
    │               │
    └───────┬───────┘
            │
    ┌───────▼───────┐
    │ Session Layer │  (Auto-detect protocol)
    └───────┬───────┘
            │
    ┌───────▼───────────────┐
    │  Message Router       │
    │  • Pub/Sub Topics     │
    │  • Queue Mode         │
    │  • Wildcard Matching  │
    └───────┬───────────────┘
            │
    ┌───────▼──────────┐
    │  LMDB Storage    │
    │  (metricmq.db)   │
    └──────────────────┘
```

## Protocol Support

### RESP Protocol (Redis-Compatible)
Human-readable text protocol. Compatible with `redis-cli`:
```bash
redis-cli -p 6379 PUBLISH test "Hello World"
redis-cli -p 6379 SUBSCRIBE test
```

### Binary Protocol
Optimized for embedded systems:
- 17-byte header overhead
- Zero-copy parsing
- Frame: `[VERSION|COMMAND|SEQ|TOPIC_LEN|PAYLOAD_LEN|TOPIC|PAYLOAD]`

Protocol is auto-detected on connection (first byte: `*` = RESP, `0x01` = Binary)

## Exactly-Once Delivery

Sequence-based ACK mechanism prevents duplicate message processing:

1. Broker assigns sequence ID to each published message
2. Subscriber receives message with sequence number
3. Subscriber sends ACK for processed sequence
4. Broker persists ACK state to LMDB
5. On reconnect, broker skips already-ACKed messages

## Performance Benchmarks

Test environment: Windows 11, Snapdragon X Plus (12-core Oryon CPU), Adreno GPU, 16GB LPDDR5x RAM, SSD

### Throughput
```
Binary Protocol: 106,390 msg/s (10KB messages)
Data Rate: 1.01 GiB/s sustained
Publish Latency: 46.5 μs per operation
```

### Persistence (LMDB)
```
Sequential Writes: 42,674 msg/s (1KB messages)
Random Reads: 1,564,210 ops/s
Storage Overhead: 60% throughput reduction vs in-memory
```

### Binary Size
```
metricmq-broker.exe: 328 KB
vs ZeroMQ: 2.3 MB (7x larger)
vs RabbitMQ: 50 MB (150x larger)
```

### Running Benchmarks

```bash
cd build/Release
.\latency_benchmark.exe
.\throughput_benchmark.exe
.\protocol_comparison_benchmark.exe
.\persistence_benchmark.exe

# JSON output
.\latency_benchmark.exe --benchmark_out=results.json --benchmark_out_format=json
```

## ESP32/Arduino Library

Lightweight client library for ESP32 and ESP8266:
- Binary protocol support
- Exactly-once delivery
- Auto-reconnect with exponential backoff
- 2KB RAM footprint

### Arduino Installation

Arduino IDE: Sketch → Include Library → Add .ZIP Library → Select `esp32-metricmq`

PlatformIO:
```ini
lib_deps = https://github.com/yourusername/MetricMQ.git#main:esp32-metricmq
```

### ESP32 Example

```cpp
#include <WiFi.h>
#include <MetricMQ.h>

MetricMQClient mqClient;

void setup() {
  WiFi.begin("SSID", "password");
  mqClient.connect("192.168.1.100", 6379);
  mqClient.subscribe("sensors/temp", [](String topic, String payload) {
    Serial.println(payload);
  });
}

void loop() {
  mqClient.loop();
  mqClient.publish("sensors/temp", "25.5");
  delay(5000);
}
```

Supported: ESP32, ESP8266, Arduino with network capability

## API Reference

### C++ Client

RESP Protocol:
```cpp
#include "metricmq/pubsub.hpp"

metricmq::Publisher pub("127.0.0.1", 6379);
pub.send("topic", "payload");

metricmq::Subscriber sub("127.0.0.1", 6379);
sub.subscribe("topic", [](const std::string& topic, const std::string& payload) {
    std::cout << payload << "\n";
});
```

Binary Protocol:
```cpp
#include "metricmq/binary_pubsub.hpp"

metricmq::BinaryPublisher pub("127.0.0.1", 6379);
pub.publish("topic", "payload");

metricmq::BinarySubscriber sub("127.0.0.1", 6379, "client-id");
sub.subscribe("topic", [](const BinaryMessage& msg) {
    std::cout << msg.sequence << ": " << msg.payload << "\n";
});
```

### Example Programs

RESP Protocol:
```bash
.\pub_only.exe
.\sub_only.exe
.\simple_pub_sub.exe
```

Binary Protocol:
```bash
.\binary_pub_only.exe
.\binary_sub_only.exe
```

Queue Mode:
```bash
.\push_only.exe
.\pull_only.exe
```

Testing:
```bash
.\persistence_test.exe
.\exactly_once_test.exe
```

## Monitoring

Prometheus metrics available at `http://localhost:9091/metrics`:

```
metricmq_messages_published_total
metricmq_messages_delivered_total
metricmq_publish_latency_microseconds
metricmq_active_connections
metricmq_active_topics
```

## Testing

```bash
cd build
ctest -C Release
```

## License

MIT License - see LICENSE file
