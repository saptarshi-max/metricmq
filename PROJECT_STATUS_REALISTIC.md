# MetricMQ - Current Project Status

**Date**: January 2, 2026
**Version**: 0.1.0-alpha (Pre-release)

---

## 🎯 What MetricMQ Actually Is

MetricMQ is an **experimental lightweight message broker** designed specifically for **IoT and embedded systems**. The project's primary goal is to create a messaging solution that can run on **ESP32 microcontrollers** with limited RAM (520KB) and flash (4MB).

### Primary Selling Point
**IoT/MCU Friendly Architecture**
- Small binary size (~328 KB on Windows, smaller on embedded)
- Minimal memory footprint (target: <100 KB RAM for broker core)
- No dynamic allocations in critical path
- Works with constrained devices (ESP32, STM32, etc.)
- Binary protocol optimized for low bandwidth

---

## ✅ What's Currently Implemented

### Core Broker (Working)
- ✅ TCP server listening on port 6379
- ✅ Multi-threaded session handling (one thread per client)
- ✅ Topic-based message routing
- ✅ Thread-safe subscription management
- ✅ Wildcard subscriptions (`#` for all topics)

### Messaging Patterns (Working)
- ✅ **Pub/Sub** - Publisher broadcasts, all subscribers receive
- ✅ **Queue Mode (PUSH/PULL)** - Round-robin task distribution using topic prefix `q:`

### Protocol Support (Working)
- ✅ **RESP Protocol** - Redis-compatible text protocol
  - Can test with `redis-cli` or telnet
  - Commands: SUBSCRIBE, PUBLISH, PING
- ✅ **Binary Protocol** - Custom 16-byte header format
  - Smaller overhead for small messages
  - Defined command types (SUBSCRIBE, PUBLISH, MESSAGE, ACK, etc.)
- ✅ **Auto-Detection** - Broker detects protocol on first byte

### Persistence (Working)
- ✅ **LMDB Integration** - Messages stored to disk
- ✅ **Sequence ID tracking** - Every message gets unique ID
- ✅ **Historical replay** - New subscribers receive past messages
- ✅ **Survives broker restart** - Messages persist across crashes

### Exactly-Once Semantics (Partial)
- ✅ Broker tracks sequence IDs and pending ACKs
- ✅ ACK command supported in both protocols
- ✅ Session ACK handling implemented
- ⏳ Client-side deduplication logic (needs implementation)
- ⏳ Retry mechanism on timeout (needs implementation)

### Build System (Working)
- ✅ CMake build configuration
- ✅ Conan dependency management
- ✅ Windows MSVC compilation
- ✅ Multiple example programs (10+ executables)

---

## ❌ What's NOT Implemented Yet

### Critical Missing Features
- ❌ **ESP32/Embedded testing** - Never tested on actual hardware
- ❌ **Memory profiling** - No measurement of actual RAM usage
- ❌ **Throughput benchmarks** - Claims like "1.8M msg/s" are unverified
- ❌ **Load testing** - Unknown behavior under heavy load
- ❌ **Error handling** - Minimal error checking, will crash on edge cases
- ❌ **Logging system** - Only std::cout/std::cerr debug prints
- ❌ **Configuration** - Hardcoded port, no config file
- ❌ **Security** - No authentication, encryption, or access control
- ❌ **Metrics endpoint** - Prometheus /metrics not implemented
- ❌ **Client libraries** - Only C++ clients exist (no Python, JS, Rust)
- ❌ **Documentation** - API docs incomplete, no tutorials
- ❌ **Tests** - No unit tests, integration tests, or CI/CD

### Known Issues
- ⚠️ Persistence test requires manual multi-terminal setup
- ⚠️ Client deduplication not fully implemented
- ⚠️ No retention policy (database grows forever)
- ⚠️ No message TTL (time-to-live)
- ⚠️ No connection pooling or limits
- ⚠️ No graceful shutdown (Ctrl+C kills immediately)
- ⚠️ Windows-specific socket code needs #ifdef cleanup

---

## 📊 Honest Performance Assessment

### What We Know
- **Binary size**: 328 KB on Windows (MSVC Release build)
- **Compiles successfully**: All 10+ targets build without errors
- **Basic functionality**: Pub/Sub and Queue modes work in simple tests

### What We DON'T Know (Needs Testing)
- ❓ **Actual throughput** - Never measured with proper benchmark
- ❓ **Latency** - No p50/p95/p99 measurements
- ❓ **Memory usage** - Unknown RAM consumption under load
- ❓ **Connection limits** - How many concurrent clients can it handle?
- ❓ **Message size limits** - What happens with 1MB payloads?
- ❓ **Crash recovery** - Does persistence actually work after unexpected shutdown?
- ❓ **Cross-platform** - Only tested on Windows so far

### Comparisons Are Premature
**We cannot honestly compare to ZeroMQ, RabbitMQ, or NanoMQ** because:
1. No benchmarks have been run
2. Different architecture (they're battle-tested, we're experimental)
3. Missing critical features (security, monitoring, etc.)
4. Untested on actual IoT hardware

---

## 🎓 How Benchmarking Should Work

### Current Benchmark Tools
We have two benchmark programs:

#### 1. `protocol_benchmark.cpp`
**What it does**: Calculates message size overhead for RESP vs Binary protocols

```cpp
// Example: 64-byte payload
RESP overhead:   120 bytes total (56 bytes overhead = 87%)
Binary overhead: 80 bytes total (16 bytes overhead = 25%)

Conclusion: Binary is 33% smaller for small messages
```

**What it DOESN'T do**: Measure actual network throughput

#### 2. `throughput.cpp`
**Current state**: EXISTS in codebase but needs proper implementation

**What it SHOULD do**:
```cpp
// Proper throughput benchmark
1. Start broker
2. Connect N publishers and M subscribers
3. Publishers send X messages each
4. Measure:
   - Messages per second
   - Latency (time from publish to receive)
   - CPU usage
   - Memory usage
5. Report statistics (avg, p50, p95, p99, max)
```

### Realistic Benchmark Plan

**Phase 1: Desktop Testing**
```bash
# Test 1: Single pub/sub throughput
- 1 publisher sending 100K messages
- 1 subscriber receiving
- Measure: msg/sec, latency, memory

# Test 2: Multiple subscribers
- 1 publisher, 10 subscribers
- Measure: fan-out performance

# Test 3: Multiple publishers
- 10 publishers, 1 subscriber
- Measure: contention handling

# Test 4: Queue mode
- 10 producers, 10 consumers
- Measure: load balancing efficiency
```

**Phase 2: Embedded Testing** (CRITICAL - See next section)
- Test on actual ESP32 hardware
- Measure RAM/Flash usage
- Test with limited bandwidth (WiFi, BLE)

---

## 🎯 ESP32/IoT Testing Strategy

### Why ESP32 Testing is Critical

**This is the MAIN selling point** - MetricMQ is only valuable if it actually works on resource-constrained devices.

### Option 1: Wokwi Simulator Testing

**Wokwi** (https://wokwi.com) is a free online ESP32 simulator.

**Setup Steps**:

1. **Create minimal MetricMQ client for ESP32**:
```cpp
// esp32_client.cpp (Arduino-compatible)
#include <WiFi.h>

const char* ssid = "Wokwi-GUEST";
const char* password = "";
const char* broker_host = "192.168.1.100";  // Your PC IP
const uint16_t broker_port = 6379;

WiFiClient client;

void setup() {
  Serial.begin(115200);
  
  // Connect to WiFi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  // Connect to broker
  if (client.connect(broker_host, broker_port)) {
    Serial.println("Connected to MetricMQ broker!");
    
    // Send SUBSCRIBE command (RESP protocol)
    client.println("*2\r\n$9\r\nSUBSCRIBE\r\n$11\r\nsensor/temp\r\n");
  }
}

void loop() {
  // Publish temperature every 5 seconds
  static unsigned long lastPublish = 0;
  if (millis() - lastPublish > 5000) {
    float temp = random(200, 300) / 10.0;  // 20.0 - 30.0°C
    
    // PUBLISH command (RESP)
    String cmd = "*3\r\n$7\r\nPUBLISH\r\n$11\r\nsensor/temp\r\n$";
    cmd += String(temp).length();
    cmd += "\r\n" + String(temp) + "\r\n";
    
    client.print(cmd);
    Serial.println("Published: " + String(temp));
    lastPublish = millis();
  }
  
  // Receive messages
  while (client.available()) {
    String response = client.readStringUntil('\n');
    Serial.println("Received: " + response);
  }
}
```

2. **Create Wokwi project**:
   - Go to https://wokwi.com/projects/new/esp32
   - Paste the code above
   - Add `diagram.json`:
```json
{
  "version": 1,
  "author": "Your Name",
  "editor": "wokwi",
  "parts": [
    { "type": "wokwi-esp32-devkit-v1", "id": "esp", "top": 0, "left": 0 }
  ],
  "connections": []
}
```

3. **Test scenario**:
```
PC (Broker)  <--WiFi-->  Wokwi ESP32 (Client)
Port 6379                Publishes sensor data
             <--RESP-->  Receives commands
```

**Limitations of Wokwi**:
- Simulated hardware (not real performance)
- Limited RAM simulation
- Network latency may differ
- No actual GPIO/sensor integration

### Option 2: Real ESP32 Hardware Testing

**Hardware Needed** (~$10-20):
- ESP32 DevKit (ESP32-WROOM-32)
- USB cable
- Sensors (optional): DHT22 temperature sensor

**Test Setup**:

```
[ESP32] --WiFi--> [PC/Raspberry Pi running MetricMQ Broker]
  |
  Temperature Sensor (DHT22)
```

**Memory Constraints**:
```
ESP32-WROOM-32 Specs:
- Flash: 4 MB
- SRAM: 520 KB
- Wi-Fi/BT: 448 KB reserved
- Available: ~70 KB for application

MetricMQ Client Requirements:
- Binary protocol parser: ~5 KB
- TCP buffer (1KB): 1 KB
- Message queue (10 messages): ~2 KB
- WiFi stack overhead: ~40 KB
Total estimate: ~50 KB RAM needed
```

**ESP32 Client Library** (to be created):
```cpp
// metricmq_esp32.h
class MetricMQClient {
public:
  MetricMQClient(const char* host, uint16_t port);
  
  bool connect();
  void publish(const char* topic, const char* payload);
  void subscribe(const char* topic, void (*callback)(const char*, const char*));
  void loop();  // Process incoming messages
  
private:
  WiFiClient client_;
  char recv_buffer_[512];  // Small buffer for embedded
  size_t buffer_pos_;
};
```

### Option 3: ESP-IDF (Professional Approach)

**ESP-IDF** is Espressif's official development framework (more control than Arduino).

**Advantages**:
- Better memory management
- FreeRTOS integration
- Precise RAM/Flash measurements
- Production-ready

**Example metrics you can measure**:
```c
// In ESP-IDF
size_t free_heap = esp_get_free_heap_size();
size_t min_free_heap = esp_get_minimum_free_heap_size();
printf("Free heap: %d bytes\n", free_heap);
printf("Min free heap: %d bytes\n", min_free_heap);
```

---

## 🔧 What Should Be Done Next

### Priority 1: ESP32 Proof-of-Concept (CRITICAL)
1. Create minimal RESP client for ESP32 (Arduino)
2. Test on Wokwi simulator
3. Test on real ESP32 hardware
4. Measure actual RAM/Flash usage
5. Document results

**Success criteria**: ESP32 can publish sensor data and receive commands with <70 KB RAM usage

### Priority 2: Basic Benchmarking
1. Implement proper throughput measurement
2. Run on desktop (Windows/Linux)
3. Measure messages/sec, latency, memory
4. Document realistic performance numbers

**Success criteria**: Have honest numbers to show (even if modest)

### Priority 3: Error Handling & Stability
1. Add proper error checking
2. Implement logging framework
3. Add graceful shutdown
4. Handle edge cases (disconnect, timeout, etc.)

### Priority 4: Documentation
1. API reference for client libraries
2. ESP32 integration guide
3. Protocol specification
4. Deployment guide

---

## 📝 Honest Limitations

### Not a Replacement For
- ❌ **RabbitMQ** - If you need enterprise features, use RabbitMQ
- ❌ **Kafka** - If you need log-based streaming, use Kafka
- ❌ **MQTT** - If you need IoT standard, use Mosquitto MQTT broker

### When to Use MetricMQ
- ✅ Embedded Linux devices (Raspberry Pi, BeagleBone)
- ✅ ESP32 with WiFi connectivity
- ✅ Edge gateways aggregating sensor data
- ✅ Learning message broker internals
- ✅ Prototyping IoT architectures

### When NOT to Use
- ❌ Production banking/payment systems (use battle-tested solutions)
- ❌ High-security applications (no encryption/auth yet)
- ❌ Massive scale (1000+ clients) - untested
- ❌ Critical infrastructure - too experimental

---

## 🎯 Realistic Vision

### Short Term (1-2 months)
- ✅ Get ESP32 client working
- ✅ Publish ESP32 example on GitHub
- ✅ Run basic benchmarks
- ✅ Add proper logging

### Medium Term (3-6 months)
- Build community around ESP32 use cases
- Add Python/JavaScript client libraries
- Implement Prometheus metrics
- Create tutorial videos

### Long Term (6-12 months)
- Consider Rust rewrite for safety
- Add TLS/SSL encryption
- Implement authentication
- Production hardening

---

## 📊 Current File Structure

```
MetricMQ/
├── src/
│   ├── broker.cpp           (✅ Working - TCP server, routing)
│   ├── session.cpp          (✅ Working - Per-client handler)
│   ├── resp_parser.cpp      (✅ Working - RESP protocol)
│   ├── binary_protocol.cpp  (✅ Working - Binary protocol)
│   ├── pubsub.cpp           (✅ Working - Pub/Sub clients)
│   ├── binary_pubsub.cpp    (✅ Working - Binary clients)
│   ├── queue.cpp            (✅ Working - Queue mode)
│   └── storage/
│       └── LmdbStorage.cpp  (✅ Working - Persistence)
├── include/metricmq/        (✅ Public headers)
├── examples/                (✅ 10+ working demos)
├── benchmark/
│   ├── protocol_benchmark.cpp  (✅ Size comparison)
│   └── throughput.cpp          (⏳ Needs implementation)
├── tests/                   (❌ No tests yet)
├── esp32/                   (❌ To be created)
│   ├── arduino/             (Arduino ESP32 client)
│   ├── esp-idf/             (ESP-IDF client)
│   └── wokwi/               (Wokwi simulator projects)
└── docs/
    ├── BINARY_PROTOCOL.md   (✅ Protocol spec)
    ├── PERSISTENCE.md       (✅ LMDB integration)
    └── ESP32_GUIDE.md       (❌ To be created)
```

---

## 🎓 Learning Value

Even if MetricMQ never reaches production use, it's valuable for:
- Understanding message broker architecture
- Learning LMDB embedded databases
- Practicing C++ networking
- Exploring IoT communication patterns
- Protocol design (RESP vs Binary)

---

## 📄 License

MIT License - Free to use, modify, and distribute.

**But**: No warranty. Use at your own risk. Not production-ready.

---

**Bottom Line**: MetricMQ is an **experimental IoT message broker** with potential, but needs extensive testing on actual ESP32 hardware before making any performance claims. The focus should be on proving it works well on resource-constrained devices, not competing with established enterprise solutions.
