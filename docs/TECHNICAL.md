# MetricMQ — Technical Reference

**Version:** 0.1.0
**Language:** C++20
**Compiler:** MSVC 19.x / GCC 12+ / Clang 14+
**Dependencies:** libsodium, LMDB, spdlog, Poco (HTTP)

---

## Table of Contents

1. [Architecture Overview](#1-architecture-overview)
2. [Repository Layout](#2-repository-layout)
3. [Core Concepts](#3-core-concepts)
4. [Wire Protocol Reference](#4-wire-protocol-reference)
5. [Public API Reference](#5-public-api-reference)
6. [Security Layer](#6-security-layer)
7. [Persistence Layer](#7-persistence-layer)
8. [Metrics & Observability](#8-metrics--observability)
9. [Defensive Programming Practices](#9-defensive-programming-practices)
10. [Build System](#10-build-system)
11. [Testing Guide](#11-testing-guide)
12. [ESP32 Client Library](#12-esp32-client-library)
13. [Architectural Limitations & Known Issues](#13-architectural-limitations--known-issues)

---

## 1. Architecture Overview

MetricMQ is a single-process, multi-threaded message broker. Every accepted TCP connection spawns a dedicated session thread. The broker core is a shared state object protected by one global mutex.

```
                        ┌─────────────────────────────────────┐
                        │         metricmq-broker (PID)        │
                        │                                       │
  TCP :6379  ──────────►│  Broker::run()   (main thread)       │
                        │   accept() loop                       │
                        │   sessions_ vector                    │
                        │         │                             │
                        │   ┌─────┼──────────────────┐         │
                        │   │     │                  │         │
                        │   ▼     ▼                  ▼         │
                        │  [Session] [Session] ... [Session]   │
                        │  (thread)  (thread)       (thread)   │
                        │     │                                 │
                        │     ▼                                 │
                        │  ┌─────────────────────────────┐     │
                        │  │  Broker (shared, mutex lock) │     │
                        │  │  topic_subscribers_ map      │     │
                        │  │  client_acks_ (BoundedAckSet)│     │
                        │  │  current_sequence_ counter   │     │
                        │  └──────────────┬──────────────┘     │
                        │                 │                     │
                        │         ┌───────┴──────┐             │
                        │         │ LmdbStorage  │             │
                        │         │ (1 GB mmap)  │             │
                        │         └──────────────┘             │
                        │                                       │
  HTTP :9091  ─────────►│  MetricsServer (Poco thread pool)    │
                        └─────────────────────────────────────┘
```

### Thread Model

| Thread | Purpose | Count |
|---|---|---|
| Main thread | `broker.run()` accept loop | 1 |
| Session threads | `session.run()` recv/parse/dispatch | 1 per client (max 1000) |
| Poco thread pool | HTTP `/metrics` requests | Configured by Poco |

All broker state (`topic_subscribers_`, `client_acks_`, `sessions_`, `current_sequence_`) is protected by a single `std::mutex`. Session threads acquire this lock on every publish, subscribe, unsubscribe, and ACK.

---

## 2. Repository Layout

```
MetricMQ/
├── src/                        # Internal implementation (not installed)
│   ├── main.cpp                # Broker entry point
│   ├── broker.hpp / .cpp       # Core broker: routing, fan-out, persistence
│   ├── session.hpp / .cpp      # Per-connection session: protocol mux, message dispatch
│   ├── binary_protocol.hpp / .cpp  # Binary frame serializer / parser
│   ├── resp_parser.hpp / .cpp  # RESP (Redis) protocol parser
│   ├── pubsub.cpp              # RESP pub/sub client implementation
│   ├── binary_pubsub.cpp       # Binary pub/sub client implementation
│   ├── queue.cpp               # PUSH/PULL queue client implementation
│   ├── logger.cpp              # spdlog multi-sink initialization
│   ├── metrics.cpp             # Prometheus metrics singleton
│   ├── metrics_server.cpp      # Poco HTTP /metrics handler
│   ├── crypto/
│   │   ├── signing.hpp / .cpp  # Ed25519 sign/verify (libsodium)
│   │   └── keystore.cpp        # TrustedKeyStore global singleton
│   ├── storage/
│   │   └── LmdbStorage.hpp / .cpp  # LMDB message + ACK persistence
│   └── metrics/
│       └── PrometheusExporter.hpp / .cpp  # [STUB] unused placeholder
│
├── include/metricmq/           # Public API headers (installed)
│   ├── binary_protocol.hpp     # Frame types and parser (public copy)
│   ├── binary_pubsub.hpp       # BinaryPublisher / BinarySubscriber
│   ├── broker.hpp              # Minimal public Broker facade
│   ├── client.hpp              # Thin text (RESP) client facade
│   ├── config.hpp              # Config struct (port, persistence)
│   ├── crypto.hpp              # Ed25519 API + TrustedKeyStore
│   ├── logger.hpp              # Logger singleton + LOG_* macros
│   ├── Message.hpp             # Message{topic, payload} struct
│   ├── metrics.hpp             # Metrics singleton
│   ├── metrics_server.hpp      # MetricsServer (Poco HTTP)
│   ├── pubsub.hpp              # Publisher / Subscriber (RESP)
│   ├── queue.hpp               # QueueProducer / QueueConsumer
│   ├── types.hpp               # Topic / Payload type aliases
│   └── version.hpp             # METRICMQ_VERSION constant
│
├── examples/                   # Runnable example programs
├── benchmark/                  # Google Benchmark suites
├── tools/                      # CLI utilities (keygen)
├── scripts/                    # PowerShell scripts
├── esp32-metricmq/             # Arduino library for ESP32
├── metricmq-platformio-test/   # PlatformIO ESP8266 project
├── wokwi/                      # Browser-simulatable Wokwi projects
├── docs/                       # Technical documentation (this file)
├── CMakeLists.txt              # Top-level build definition
└── conanfile.txt               # Conan dependency manifest
```

### Layer Diagram

```
┌────────────────── Public API (include/metricmq/) ──────────────────┐
│  Publisher  BinaryPublisher  Subscriber  BinarySubscriber  Broker   │
│  QueueProducer  QueueConsumer  crypto::*  Metrics  MetricsServer    │
└─────────────────────────────────────────────────────────────────────┘
                              │
┌────────────────── Core Engine (src/) ──────────────────────────────┐
│  Broker ← Session ← BinaryProtocol / RespParser                    │
│              │                                                       │
│          LmdbStorage        signing.cpp / keystore.cpp             │
│              │                                                       │
│          metrics.cpp        metrics_server.cpp (Poco HTTP)         │
└─────────────────────────────────────────────────────────────────────┘
                              │
┌────────────────── External Libraries ──────────────────────────────┐
│  libsodium (Ed25519)  lmdb (persistence)  spdlog (logging)         │
│  Poco::Net (HTTP)     fmt (string formatting)                       │
└─────────────────────────────────────────────────────────────────────┘
```

---

## 3. Core Concepts

### 3.1 Protocol Auto-Detection

On the first byte of every new connection, the session classifies the protocol:

| First byte | Value | Protocol |
|---|---|---|
| `+` `-` `:` `$` `*` | 0x2B, 0x2D, 0x3A, 0x24, 0x2A | RESP (Redis-compatible) |
| `0x01` | 1 | Binary (MetricMQ native) |
| anything else | — | Unknown → connection kept open waiting |

Once detected, the protocol type is fixed for the lifetime of the connection.

### 3.2 Topic Routing

Topics are exact-match strings. The broker fan-out also delivers to a single wildcard subscription: the literal topic `"#"` receives every message regardless of topic. There is no recursive wildcard matching (e.g. MQTT-style `+` or multi-level `#` patterns are not supported).

### 3.3 Exactly-Once Delivery

Each published message receives a monotonically increasing `uint64_t` sequence ID (`current_sequence_` in `Broker`). Subscribers with a registered `client_id` send an `ACK` frame back to the broker after processing each message. The broker persists ACK state to LMDB so a reconnecting client with the same `client_id` only replays unacknowledged messages.

```
Publisher ──PUBLISH──► Broker ──MESSAGE(seq=N)──► Subscriber
                         │                              │
                         │◄─────────ACK(seq=N)─────────┘
                         │
                     save_ack(client_id, N)
                         │
                    LMDB write
```

The `client_id` is embedded in the SUBSCRIBE frame's topic field using a null-byte separator:

```
topic field = "my-device-01\0sensors/temperature"
               └─ client_id ─┘  └─── actual topic ───┘
```

### 3.4 Message Persistence

All published messages are written to LMDB before being delivered to subscribers. On broker restart, subscribers that reconnect and identify themselves replay all messages with sequence IDs greater than their last-ACK'd sequence.

---

## 4. Wire Protocol Reference

### 4.1 Binary Protocol Frame Layout

Every frame begins with a fixed 16-byte header:

```
Offset  Size  Field
──────  ────  ─────────────────────────────────────────────
0       1 B   Version  (must be 0x01)
1       1 B   Command  (see Command Codes below)
2       8 B   Sequence (uint64, big-endian)
10      2 B   Topic Length (uint16, big-endian, max 256)
12      4 B   Payload Length (uint32, big-endian, max 16 MB)
16      N B   Topic  (UTF-8 string, N = Topic Length)
16+N    M B   Payload (arbitrary bytes, M = Payload Length)

For CMD_SIGNED_PUBLISH / CMD_SIGNED_MESSAGE, append after Payload:
16+N+M  64 B  Ed25519 Signature
16+N+M+64  4 B  Key ID (uint32, big-endian)
```

Total minimum frame: 16 bytes (header-only, e.g. PING/PONG/ACK).

### 4.2 Command Codes

| Hex | Name | Direction | Description |
|-----|------|-----------|-------------|
| `0x01` | `CMD_SUBSCRIBE` | Client → Broker | Subscribe to a topic |
| `0x02` | `CMD_UNSUBSCRIBE` | Client → Broker | Unsubscribe from a topic |
| `0x03` | `CMD_PUBLISH` | Client → Broker | Publish a message (unsigned) |
| `0x04` | `CMD_MESSAGE` | Broker → Client | Deliver a message |
| `0x05` | `CMD_ACK` | Bidirectional | Acknowledge receipt by sequence ID |
| `0x06` | `CMD_PING` | Either | Keepalive ping |
| `0x07` | `CMD_PONG` | Either | Keepalive response |
| `0x08` | `CMD_ERROR` | Broker → Client | Error response (payload = UTF-8 reason) |
| `0x10` | `CMD_SIGNED_PUBLISH` | Client → Broker | Publish with Ed25519 signature |
| `0x11` | `CMD_SIGNED_MESSAGE` | Broker → Client | Deliver a signed message (signature preserved) |

### 4.3 RESP Protocol (Text Mode)

The RESP protocol is Redis-compatible. Commands are sent as RESP arrays:

```
# PUBLISH
*3\r\n$7\r\nPUBLISH\r\n$5\r\ntopic\r\n$7\r\npayload\r\n

# SUBSCRIBE
*2\r\n$9\r\nSUBSCRIBE\r\n$5\r\ntopic\r\n

# PING
*1\r\n$4\r\nPING\r\n

# ACK (sequence number as integer string)
*2\r\n$3\r\nACK\r\n$1\r\n5\r\n
```

Supported RESP commands: `SUBSCRIBE`, `UNSUBSCRIBE`, `PUBLISH`, `PING`, `ACK`.

### 4.4 Parser Limits

| Limit | Binary Protocol | RESP Protocol |
|---|---|---|
| Max topic length | 256 bytes (`MAX_TOPIC_LEN`) | Governed by bulk string limit |
| Max payload / bulk string size | 16 MB (`MAX_PAYLOAD_LEN`, `MAX_BULK_LEN`) | 16 MB |
| Max RESP array elements | N/A | 10,000 (`MAX_ARRAY_COUNT`) |
| Max per-session recv buffer | 16 MB (`MAX_RECV_BUFFER`) | 16 MB |
| Max simultaneous connections | 1,000 (`MAX_CONNECTIONS`) | same |

---

## 5. Public API Reference

### 5.1 `Publisher` (RESP protocol)

```cpp
#include <metricmq/pubsub.hpp>

metricmq::Publisher pub("127.0.0.1", 6379);
pub.send("sensors/temp", "22.5");
```

**Methods**

| Signature | Description |
|---|---|
| `Publisher(host, port)` | Opens a TCP connection to the broker |
| `void send(topic, payload)` | Publishes one message using RESP PUBLISH |
| `~Publisher()` | Closes the socket |

**Thread safety:** Not thread-safe. Use one instance per thread, or external locking.

---

### 5.2 `Subscriber` (RESP protocol)

```cpp
#include <metricmq/pubsub.hpp>

metricmq::Subscriber sub("127.0.0.1", 6379);
sub.subscribe("sensors/temp", [](const std::string& topic, const std::string& payload) {
    std::cout << topic << ": " << payload << "\n";
});
sub.run();  // blocks until socket closes
```

**Methods**

| Signature | Description |
|---|---|
| `Subscriber(host, port)` | Opens a TCP connection to the broker |
| `void subscribe(topic, callback)` | Registers a subscription and message callback |
| `void run()` | Blocking receive loop; dispatches callbacks |
| `~Subscriber()` | Closes the socket |

**Notes:**
- `run()` never returns unless the broker closes the connection or an exception occurs.
- Multiple `subscribe()` calls before `run()` register multiple topic callbacks.
- Automatically sends `ACK` frames for exactly-once delivery tracking.

---

### 5.3 `BinaryPublisher` (binary protocol)

```cpp
#include <metricmq/binary_pubsub.hpp>

metricmq::BinaryPublisher pub("127.0.0.1", 6379);

// Unsigned publish
pub.send("sensors/temp", "22.5");

// Signed publish (requires key setup)
pub.setSigningKey(keypair.secret_key, keypair.key_id);
pub.sendSigned("secure/temp", "22.5");
```

**Methods**

| Signature | Description |
|---|---|
| `BinaryPublisher(host, port)` | Opens TCP, sends to binary framing |
| `void send(topic, payload)` | Publishes using `CMD_PUBLISH` |
| `void sendSigned(topic, payload)` | Signs message with Ed25519, sends `CMD_SIGNED_PUBLISH`. Requires `setSigningKey()` first. |
| `void setSigningKey(secret_key, key_id)` | Configures the 64-byte Ed25519 secret key and numeric key ID |
| `bool isSigningEnabled()` | Returns true if `setSigningKey()` has been called |

**Signed message format:** The broker verifies against `topic + payload` (concatenated, no separator). The `key_id` must match a key pre-registered via `TrustedKeyStore::register_key()`.

---

### 5.4 `BinarySubscriber` (binary protocol)

```cpp
#include <metricmq/binary_pubsub.hpp>

// Exactly-once: pass client_id to enable ACK tracking
metricmq::BinarySubscriber sub("my-device-01", "127.0.0.1", 6379);

sub.subscribe("sensors/temp",
    [](const std::string& topic, const std::string& payload) {
        std::cout << payload << "\n";
    });

// Or handle signed messages
sub.subscribeSigned("secure/temp",
    [](const metricmq::SignedMessageInfo& msg) {
        std::cout << "key_id=" << msg.key_id
                  << " signed=" << msg.is_signed << "\n";
    });
```

**Constructors**

| Signature | Description |
|---|---|
| `BinarySubscriber(host, port)` | Anonymous subscriber, no exactly-once tracking |
| `BinarySubscriber(client_id, host, port)` | Identified subscriber; enables replay-on-reconnect |

**Methods**

| Signature | Description |
|---|---|
| `void setClientId(client_id)` | Set/override client identifier |
| `void subscribe(topic, callback, auto_ack)` | Subscribe; `auto_ack=true` sends `CMD_ACK` after each message |
| `void subscribeSigned(topic, callback, auto_ack)` | Subscribe with access to signature metadata |
| `void run()` | Blocking receive loop (for use after `subscribe()`) |

**`SignedMessageInfo` fields:**

| Field | Type | Description |
|---|---|---|
| `topic` | `std::string` | Message topic |
| `payload` | `std::string` | Message payload |
| `is_signed` | `bool` | True when received as `CMD_SIGNED_MESSAGE` |
| `key_id` | `uint32_t` | Key identifier (0 if unsigned) |
| `signature` | `std::array<uint8_t,64>` | Raw Ed25519 signature bytes |
| `sequence` | `uint64_t` | Broker sequence ID |

---

### 5.5 `QueueProducer` / `QueueConsumer`

```cpp
#include <metricmq/queue.hpp>

metricmq::QueueProducer prod("127.0.0.1", 6379);
prod.push("tasks", "job payload");

metricmq::QueueConsumer cons("127.0.0.1", 6379);
cons.pull("tasks", [](const std::string& payload) {
    process(payload);
});
```

**Notes:** Queue semantics are implemented at the broker by routing messages to topic subscribers in round-robin order. The `QueueConsumer::pull()` call blocks until a message arrives.

---

### 5.6 `Broker` (public facade)

```cpp
#include <metricmq/broker.hpp>

metricmq::Broker broker(6379);
broker.run();  // blocks
```

The public header exposes only `Broker(port)` and `run()`. `stop()` is called internally via signal handler. All internal state lives in `src/broker.hpp`.

---

### 5.7 Crypto API (`metricmq::crypto`)

```cpp
#include <metricmq/crypto.hpp>

// One-time initialization (call before any other crypto function)
metricmq::crypto::init();

// Key generation
auto kp = metricmq::crypto::generate_keypair();
// kp.public_key  — 32 bytes
// kp.secret_key  — 64 bytes (libsodium expanded format)
// kp.key_id      — assigned by keystore on registration

// Deterministic key (for embedded devices with fixed seeds)
std::array<uint8_t,32> seed = { /* 32 bytes */ };
auto kp2 = metricmq::crypto::generate_keypair_from_seed(seed);

// Sign
auto sig = metricmq::crypto::sign("hello", kp.secret_key);

// Verify
bool ok = metricmq::crypto::verify("hello", sig, kp.public_key);

// Hex utilities
std::string hex = metricmq::crypto::to_hex(kp.public_key);
auto maybe_key = metricmq::crypto::public_key_from_hex(hex);

// Secure erase (call on secret_key before destruction)
metricmq::crypto::secure_zero(kp.secret_key);
```

**All crypto functions are `[[nodiscard]]`** — the compiler will warn if you ignore a return value.

---

### 5.8 `TrustedKeyStore`

```cpp
// Broker side: register a device's public key
auto& ks = metricmq::crypto::get_global_keystore();

uint32_t key_id = ks.register_key(
    device_public_key,
    "esp32-sensor-01",       // device name (for logs)
    "sensors/*"              // topic scope (see below)
);

// Or register with a fixed key_id (for pre-provisioned devices)
ks.register_key(0xDEADBEEF, device_public_key, "device-01", "secure/*");

// Look up
auto maybe_key = ks.get_key(key_id);
auto maybe_info = ks.get_key_info(key_id);

// Revoke
ks.set_enabled(key_id, false);
ks.remove_key(key_id);
```

**Topic scope rules** (`allowed_topics` field):

| Value | Match behaviour |
|---|---|
| `"*"` or `"#"` | All topics |
| `"sensors/*"` | Prefix match — any topic starting with `"sensors/"` |
| `"secure/data"` | Exact match only |
| `""` (empty) | No restriction |

---

### 5.9 `Logger`

```cpp
#include <metricmq/logger.hpp>

// Initialize once (done automatically in main.cpp)
metricmq::Logger::init("logs/metricmq.log", spdlog::level::debug);

// Log macros (printf-style via {fmt})
LOG_TRACE("low-level trace: value={}", x);
LOG_DEBUG("debug: fd={}", sock_fd);
LOG_INFO("client connected: id={}", client_id);
LOG_WARN("unusual condition: {}", msg);
LOG_ERROR("recoverable error: {}", err.what());
LOG_CRITICAL("fatal: {}", reason);
```

Sinks: colored stdout (INFO and above) + rotating file (DEBUG and above, max 5 MB × 3 files).

---

### 5.10 `Metrics`

```cpp
#include <metricmq/metrics.hpp>

auto& m = metricmq::Metrics::instance();

// Counters (monotonically increasing)
m.incrementPublished();
m.incrementDelivered();
m.incrementConnections();
m.incrementDisconnections();
m.incrementAcks();
m.incrementErrors();

// Gauges (current snapshot)
m.setActiveConnections(n);
m.setActiveSubscribers(n);
m.setTopicCount(n);

// Latency histograms (microseconds)
m.recordPublishLatency(latency_us);
m.recordDeliveryLatency(latency_us);

// Per-topic
m.incrementTopicMessages("sensors/temp");
m.incrementTopicSubscribers("sensors/temp");
m.decrementTopicSubscribers("sensors/temp");

// Prometheus text export
std::string prom = m.exportPrometheus();
```

Buckets: `<100 µs`, `<500 µs`, `<1 ms`, `<5 ms`, `<10 ms`, `<50 ms`, `<100 ms`, `+Inf`.

---

## 6. Security Layer

### 6.1 Signed Publish Flow

```
  Device (ESP32)                    Broker
  ─────────────                     ──────────────────────────────
  1. sign(topic + payload, sk)
  2. CMD_SIGNED_PUBLISH ──────────► 3. verify_with_key(key_id,
                                         topic + payload, sig)
                                    4. check allowed_topics scope
                                    5a. [invalid] → CMD_ERROR
                                    5b. [valid]   → CMD_SIGNED_MESSAGE
                                                    fan-out to subscribers
```

**Message to sign:** `topic + payload` (raw bytes, no separator, no null terminator). This is the format used by `session.cpp` and the ESP32 library.

> **Warning:** `examples/crypto_demo.cpp` uses the format `topic + ":" + payload`. This file is a standalone demonstration only; it does NOT interact with the broker's verify path.

### 6.2 Topic Enforcement

Topics beginning with `"secure/"` are **reject-on-unsigned**. Any `CMD_PUBLISH` frame targeting a `secure/` prefix returns `CMD_ERROR`. Only `CMD_SIGNED_PUBLISH` with a valid, registered key and an authorized topic scope is accepted.

### 6.3 Key Revocation

Call `TrustedKeyStore::set_enabled(key_id, false)` to immediately block all future publishes from a compromised device. The key entry remains in the store (for audit purposes) but `verify_with_key()` returns false for disabled keys.

### 6.4 Crypto Primitives

| Primitive | Algorithm | Library | Key sizes |
|---|---|---|---|
| Signature | Ed25519 | libsodium `crypto_sign_*` | PK: 32 B, SK: 64 B, Sig: 64 B |
| Seed-based keygen | SHA-512 KDF | libsodium `crypto_sign_seed_keypair` | Seed: 32 B |
| Secure erase | `sodium_memzero` | libsodium | Arbitrary |

---

## 7. Persistence Layer

### 7.1 Storage Schema

All data is stored in a single LMDB database file (`metricmq.db`) using a flat key space:

| Key pattern | Value | Description |
|---|---|---|
| `last_seq` | `uint64_t` (8 bytes, native-endian) | Highest sequence ID written |
| `msg:<seq>` | `topic\x00payload` | Published message, null-separated |
| `ack:<client_id>:<seq>` | `"1"` (1 byte) | Per-client ACK record |

`<seq>` is the decimal string representation of the sequence number.

### 7.2 LmdbStorage API

```cpp
#include "storage/LmdbStorage.hpp"

metricmq::storage::LmdbStorage db("metricmq.db");

// Write a message
db.save(seq, topic, payload);

// Load a range for replay
auto msgs = db.load_range(from_seq, to_seq);
// Returns: vector<tuple<uint64_t seq, string topic, string payload>>

// Get the highest written sequence
uint64_t last = db.get_last_seq();

// Persist an ACK
db.save_ack(client_id, seq);

// Load all ACKs for a reconnecting client
auto acks = db.load_acks(client_id);
// Returns: unordered_set<uint64_t>

// Delete old messages (seq <= max_seq_to_delete)
db.compact(max_seq_to_delete);

// Delete stale ACK records (seq <= max_seq_to_delete)
db.purge_old_acks(max_seq_to_delete);
```

> **When compaction runs:** The broker's `publish()` increments `publish_count_` on every
> message; every 1,000 publishes it calls `runCompactionIfDue()`, which passes
> `current_sequence_ - MAX_STORED_MESSAGES` (100,000) as the threshold to both methods.
> This keeps LMDB below ~100 MB for typical 1 KB messages.

### 7.3 LMDB Configuration

```cpp
mdb_env_set_mapsize(env_, 1024ULL * 1024 * 1024);  // 1 GB virtual address space
mdb_env_open(env_, path, MDB_NOSUBDIR | MDB_WRITEMAP, 0664);
```

`MDB_WRITEMAP` enables direct memory-mapped writes (faster, but requires the OS to flush on crash). `MDB_NOSUBDIR` treats the path as a file, not a directory.

### 7.4 Replay on Reconnect

When a client reconnects with an existing `client_id`:
1. Broker calls `load_acks(client_id)` to restore the acknowledgement set.
2. `last_ack_seq_[client_id]` is set to `max(ack_set)`.
3. `replayMessagesForClient()` calls `load_range(last_ack + 1, last_ack + 1_000_000)` and sends unACK'd messages.

> **Known limitation:** `replayMessagesForClient` loads up to 1,000,000 messages per reconnect call. For high-volume topics, this can significantly increase reconnect latency.

---

## 8. Metrics & Observability

### 8.1 Prometheus Endpoint

```
GET http://localhost:9091/metrics
Content-Type: text/plain; version=0.0.4
```

### 8.2 Exposed Metrics

| Metric name | Type | Description |
|---|---|---|
| `metricmq_messages_published_total` | Counter | Total messages received from publishers |
| `metricmq_messages_delivered_total` | Counter | Total message deliveries to subscribers |
| `metricmq_subscriptions_total` | Counter | Total subscribe commands received |
| `metricmq_unsubscriptions_total` | Counter | Total unsubscribe commands received |
| `metricmq_connections_total` | Counter | Total accepted TCP connections |
| `metricmq_disconnections_total` | Counter | Total closed TCP connections |
| `metricmq_acks_total` | Counter | Total ACK frames processed |
| `metricmq_errors_total` | Counter | Total error frames sent |
| `metricmq_active_connections` | Gauge | Current open connections |
| `metricmq_active_subscribers` | Gauge | Current subscription count |
| `metricmq_topic_count` | Gauge | Number of distinct active topics |
| `metricmq_publish_latency_us` | Histogram | End-to-end publish latency (µs) |
| `metricmq_delivery_latency_us` | Histogram | Message delivery latency (µs) |
| `metricmq_topic_messages_total{topic}` | Counter | Per-topic message count |
| `metricmq_topic_subscribers{topic}` | Gauge | Per-topic subscriber count |

### 8.3 Latency Histogram Buckets

`0.0001`, `0.0005`, `0.001`, `0.005`, `0.010`, `0.050`, `0.100` seconds, `+Inf`.

### 8.4 Quick Test

```powershell
Invoke-WebRequest http://localhost:9091/metrics | Select-Object -ExpandProperty Content
```

---

## 9. Defensive Programming Practices

This section documents the specific hardening measures implemented in the codebase, and the reasoning behind each one.

### 9.1 Crash Isolation (`session.cpp`)

```cpp
// Session::run() — entire recv loop is wrapped
try {
    while (true) { /* recv / parse / dispatch */ }
} catch (const std::exception& e) {
    LOG_ERROR("Session fd={} threw: {} — closing connection", sock_fd_, e.what());
} catch (...) {
    LOG_ERROR("Session fd={} threw unknown exception", sock_fd_);
}
```

**Why:** Session threads are detached. An uncaught exception in a detached thread calls `std::terminate()`, killing the entire broker process. The catch-all ensures one bad client cannot affect other clients.

### 9.2 Receive Buffer Cap (`session.cpp`)

```cpp
static constexpr size_t MAX_RECV_BUFFER = 16 * 1024 * 1024;  // 16 MB

if (recv_buffer_.size() + static_cast<size_t>(n) > MAX_RECV_BUFFER) {
    LOG_WARN("recv_buffer_ exceeded limit on fd={} — dropping connection", sock_fd_);
    sendBinary(BinaryFrame::error("Message too large"));
    break;
}
```

**Why:** Without this guard, a slow-drip client (slow loris style) or a client with a huge claimed payload length accumulates data indefinitely, exhausting broker RAM.

### 9.3 Binary Frame Size Limits (`binary_protocol.cpp`)

```cpp
if (topic_len > MAX_TOPIC_LEN)   return std::nullopt;  // 256 bytes
if (payload_len > MAX_PAYLOAD_LEN) return std::nullopt;  // 16 MB
```

**Why:** A malicious frame with `payload_len = 0xFFFFFFFF` would cause `recv_buffer_` to accumulate 4 GB waiting for data that never arrives, even before the 16 MB `MAX_RECV_BUFFER` cap kicks in.

### 9.4 Version Mismatch Handling (`binary_protocol.cpp`)

```cpp
if (frame.version != BINARY_PROTOCOL_VERSION) {
    return std::nullopt;  // graceful drop, not throw
}
```

**Why:** Throwing here propagates to `Session::run()` where it is caught, but returning `nullopt` is semantically cleaner — the frame is silently dropped without closing the connection, allowing the client to send a corrected frame.

### 9.5 RESP Parser Hardening (`resp_parser.cpp`)

```cpp
// All stoll() calls are guarded
try { len = std::stoll(len_str); }
catch (...) { return std::nullopt; }

// Bulk string size cap
if (len < 0 || len > MAX_BULK_LEN) return std::nullopt;  // 16 MB

// Array count cap
if (count < 0 || count > MAX_ARRAY_COUNT) return std::nullopt;  // 10,000

// Unknown type byte → drop, don't throw
default: return std::nullopt;
```

**Why:** `stoll` throws `std::invalid_argument` / `std::out_of_range` on invalid input. Without the catch, a malformed integer in a RESP frame propagates up through the detached session thread and terminates the broker. The limits prevent amplification attacks where a tiny request causes massive allocation.

### 9.6 Connection Limit (`broker.cpp`)

```cpp
if (active_connections_.load() >= MAX_CONNECTIONS) {  // 1000
    LOG_WARN("Connection limit reached — rejecting fd={}", client);
    close(client);
    continue;
}
```

**Why:** Without a cap, each connection spawns a `std::thread` (typically ~1 MB stack). At 10,000 connections the broker would consume 10 GB of stack space before any application logic runs.

### 9.7 Bind/Listen Error Checking (`broker.cpp`)

```cpp
if (bind(server_fd, ...) < 0) {
    LOG_CRITICAL("bind() failed: errno={}", errno);
    close(server_fd);
    return;
}
if (listen(server_fd, 10) < 0) {
    LOG_CRITICAL("listen() failed: errno={}", errno);
    close(server_fd);
    return;
}
```

**Why:** Previously these return values were silently ignored. A port-already-in-use error would allow the broker to start an accept loop on an invalid socket descriptor, producing confusing errors later.

### 9.8 LMDB Map Size (`LmdbStorage.cpp`)

```cpp
mdb_env_set_mapsize(env_, 1024ULL * 1024 * 1024);  // 1 GB
```

**Why:** The previous 10 MB limit fills after approximately 33,000 average messages. When the map is full, `mdb_put()` returns `MDB_MAP_FULL`, and persistence calls silently fail (`void` return value). 1 GB is a safe upper bound for a typical IoT deployment.

---

## 10. Build System

### 10.1 Dependencies

Managed by Conan 1.x (`conanfile.txt`):

| Library | Purpose | Notes |
|---|---|---|
| `libsodium` | Ed25519 crypto | Required |
| `lmdb` | Message persistence | Required |
| `spdlog` | Structured logging | Required |
| `poco/Net` | HTTP metrics server | Required |
| `fmt` | String formatting | Via spdlog |
| `benchmark` | Google Benchmark | Test/bench only |

### 10.2 Build Steps

```bash
# Install dependencies with Conan
conan install . --output-folder=. --build=missing -s build_type=Release

# Configure with CMake
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=conan_toolchain.cmake

# Build all targets
cmake --build build --config Release

# Run the broker
./build/Release/metricmq-broker
```

### 10.3 CMake Targets

| Target | Output | Description |
|---|---|---|
| `metricmq_lib` | `metricmq_lib.lib` / `.a` | Static library with all broker internals |
| `metricmq-broker` | `metricmq-broker.exe` | Runnable broker binary |
| `metricmq-keygen` | `metricmq-keygen.exe` | Ed25519 key pair generator |
| `simple_pub_sub` | Example binary | RESP pub/sub demo |
| `binary_pub_only` | Example binary | Binary protocol publisher |
| `binary_sub_only` | Example binary | Binary protocol subscriber |
| `signed_publish_test` | Example binary | Ed25519 integration test |
| `exactly_once_test` | Example binary | Exactly-once delivery integration test |
| `persistence_test` | Example binary | Persistence integration test |
| `latency_benchmark` | Benchmark binary | Google Benchmark latency suite |
| `throughput_benchmark` | Benchmark binary | Google Benchmark throughput suite |
| `persistence_benchmark` | Benchmark binary | LMDB I/O benchmark |

---

## 11. Testing Guide

### 11.1 Ed25519 Signing Unit Test

Tests in-process without a broker. Verifies key generation, sign, verify, and hex round-trip.

```bash
./Release/signed_publish_test.exe
# Expected: all assert() calls pass, exits 0
```

### 11.2 Exactly-Once Delivery Integration Test

Requires a running broker. Tests ACK flow, replay on reconnect, and deduplication.

```bash
# Terminal 1
./Release/metricmq-broker.exe

# Terminal 2
./Release/exactly_once_test.exe
```

### 11.3 Persistence Integration Test

Publishes 5 messages, waits 3 seconds, then subscribes and verifies replay.

```bash
# Terminal 1
./Release/metricmq-broker.exe

# Terminal 2
./Release/persistence_test.exe
```

### 11.4 Metrics Endpoint

```powershell
./scripts/test_metrics.ps1
# Expected: HTTP 200, Content-Type text/plain, Prometheus output
```

### 11.5 Manual Protocol Smoke Test

```bash
# Using netcat (RESP mode)
echo -e "*3\r\n\$7\r\nPUBLISH\r\n\$5\r\nhello\r\n\$5\r\nworld\r\n" | nc localhost 6379
```

---

## 12. ESP32 Client Library

The `esp32-metricmq/` directory contains a ready-to-install Arduino library implementing the same binary framing protocol.

### 12.1 Installation

Copy `esp32-metricmq/` to your Arduino libraries folder, or install via PlatformIO:

```ini
# platformio.ini
lib_deps =
    local://esp32-metricmq
```

### 12.2 Arduino API

```cpp
#include <MetricMQ.h>

MetricMQClient client;

// Step 1: Store broker address (does NOT connect yet)
client.begin("192.168.1.100", 6379);

// Step 2: Connect (optionally pass client_id for exactly-once replay)
client.connect("esp32-sensor-01");          // identifies client; enables ACK replay on reconnect
// OR: client.connect();                    // anonymous — no replay-offset tracking

// Publish (unsigned)
client.publish("sensors/temp", "22.5");

// Publish (signed — requires setSigningKey BEFORE connect)
uint8_t secret_key[64] = { /* ... from keygen tool ... */ };
uint32_t key_id = 0x00000001;
client.setSigningKey(secret_key, key_id);
client.publishSigned("secure/temp", "22.5");  // topic + payload must fit in 1500 B frame

// Subscribe
client.subscribe("commands", [](const String& topic,
                                const uint8_t* payload, size_t len,
                                uint64_t seq) {
    // Handle command
});

// Call in loop() — drives keep-alive and receive handling
void loop() {
    // Auto-reconnect is NOT built-in — poll isConnected() yourself
    if (!client.isConnected()) {
        client.connect("esp32-sensor-01");
    }
    client.loop();
}
```

**ESP32 Hard Limits:**

| Limit | Value | Notes |
|---|---|---|
| Max outbound frame | **1500 bytes** | header(16) + topic + payload + sig(64) + keyId(4); no fragmentation |
| Receive buffer | **2048 bytes** | `recv_buffer_[2048]` — frames larger than this are not parsed |
| Max local verify keys | **4** (`MAX_VERIFY_KEYS`) | For verifying incoming `CMD_SIGNED_MESSAGE` frames |
| Keep-alive PING interval | **60 seconds** | `keep_alive_ms_` default |
| Connection timeout | **180 seconds** | Missing three keep-alives disconnects the client |
| Auto-reconnect | **Not implemented** | `loop()` calls `resetConnection()` on timeout; your sketch must retry |
| Client ID source | Defaults to empty | Call `setClientId()` or pass to `connect(clientId)` before connecting |

### 12.3 Key Provisioning Workflow

```
1. On PC: run metricmq-keygen.exe
   → Outputs C arrays for sketch and public key for broker

2. Paste secret_key[] array into ESP32 sketch

3. On broker: call TrustedKeyStore::register_key(key_id, public_key, ...)
   → Can be done at startup in a config file or via a provisioning API
```

### 12.4 Memory Footprint (ESP32-S3)

| Component | Flash | RAM |
|---|---|---|
| MetricMQClient | ~4 KB | ~1 KB |
| libsodium (ESP32 built-in) | ~40 KB | ~2 KB |
| Total overhead | ~44 KB | ~3 KB |

### 12.5 Broker Stability for Embedded Deployments

Before Phase 1, two broker-side issues could silently degrade or crash the system when running
with a fleet of ESP32 devices over days or weeks:

**ACK memory growth:** Every `CMD_ACK` from any device would insert into an unbounded hash set.
A device publishing and ACK'ing once per second for 30 days would generate ~2.6 million ACK
records for that client alone. With 50 devices, the broker could accumulate **130 million**
ACK entries before the first restart.

**After Phase 1:** `BoundedAckSet` caps this at 10,000 entries per client (FIFO eviction).
Memory per device is bounded at ~640 KB regardless of uptime.

**LMDB saturation:** At 1 KB/message with one publish per second across 50 devices,
`metricmq.db` would hit 1 GB in ~5.7 hours. After that, `mdb_put()` returns `MDB_MAP_FULL`,
persistence silently fails, and all LMDB writes (messages and ACKs) are dropped.

**After Phase 1:** `compact()` deletes messages older than the last 100,000 sequence IDs;
`purge_old_acks()` deletes corresponding ACK records. Both run every 1,000 publishes.

**Replay window for embedded devices:**
- 100,000 messages across 50 devices at 1 Hz = 2,000 seconds ≈ **33 minutes** per device
- At 0.1 Hz (polling every 10 s): **333 minutes → 5.5 hours** before messages age out
- For longer outages, increase `MAX_STORED_MESSAGES` in `broker.hpp` (default: 100,000)

---

## 13. Architectural Limitations & Known Issues

> **Phase 1 status:** The two previously Critical OOM issues — unbounded ACK sets and LMDB
> map saturation — have been resolved. See §7.2 for the compaction API and `BoundedAckSet`
> in `broker.hpp` for the bounded ACK tracking implementation.

### Critical

| Issue | Location | Impact |
|---|---|---|
| Global mutex on all hot paths | `broker.cpp` | Publish, subscribe, ACK, and session teardown all contend on one `std::mutex`. Under high fan-out (1000 subscribers, 10K msg/s), the lock becomes a throughput bottleneck. |
| `sessions_` accessed without lock in accept loop | `broker.cpp:run()` | The main thread reads `sessions_.size()` and calls `sessions_.push_back()` while session threads call `removeSession()` under the mutex. This is a latent data race on the `sessions_` vector. |

### Moderate

| Issue | Location | Impact |
|---|---|---|
| Signing format inconsistency | `crypto_demo.cpp` vs broker | `crypto_demo.cpp` signs `topic + ":" + payload` while the broker verifies `topic + payload`. These are incompatible. Do not use `crypto_demo.cpp` as a reference for device integration. |
| `replayMessages` loads up to 1M messages per reconnect | `broker.cpp:replayMessagesForClient()` | A client with a large backlog can cause seconds-long blocking on the session thread during reconnection. |
| `send()` does not handle partial writes | `session.cpp:send()` | `::send()` may return fewer bytes than requested on a loaded kernel buffer. The remainder is silently lost. |
| No topic sanitization | `session.cpp` | Arbitrary byte strings are accepted as topic names. Topics containing null bytes, path separators, or control characters are stored and routed without validation. |
| No session idle timeout | `broker.cpp` | Crashed clients leave zombie session threads alive indefinitely; each holds a file descriptor and stack allocation. |
| No RESP authentication | `session.cpp` | Any TCP client on port 6379 can publish or subscribe to any topic with no credentials. |

### Low

| Issue | Location | Impact |
|---|---|---|
| `throughput.cpp` is an empty stub | `benchmark/throughput.cpp` | This benchmark binary does nothing. Do not rely on it for throughput numbers. |
| Hardcoded Conan paths in `CMakeLists.txt` | `CMakeLists.txt` | May fail to find packages on a machine with a different Conan cache location. |
| No `ctest` integration | — | All tests are manual. CI pipelines cannot detect regressions automatically. |
| RESP `PUBLISH` returns hardcoded `1` | `session.cpp:handleCommand()` | The actual subscriber count is not returned, breaking Redis-compatible tooling that relies on this value. |

### Fixed in Phase 1 (branch: `demo/esp32-security`)

| Issue | Resolution |
|---|---|
| Unbounded `client_acks_` sets (was Critical) | Replaced with `BoundedAckSet` — deque+hash set, MAX=10,000 entries, FIFO eviction. Fixed RAM per client ≈640 KB. |
| LMDB grows unboundedly on disk (was Critical) | `LmdbStorage::compact()` + `purge_old_acks()` run every 1,000 publishes, keeping the last 100,000 messages. |

---

*Last updated: 2026-03-13*
*Branch: `demo/esp32-security`*
