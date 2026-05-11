# MetricMQ Developer Reference {#mainpage}

**MetricMQ** is a sub-millisecond C++20 message broker built for IoT and edge deployments.
It speaks both RESP (Redis-compatible) and a compact signed binary protocol, persists every
message to LMDB, and exposes Prometheus metrics over HTTP — all in a ~328 KB binary.

---

## Key Features

| Feature | Detail |
|---|---|
| **Dual protocol** | RESP (Redis wire-compatible) + compact 16-byte binary header |
| **Ed25519 signing** | Per-message libsodium signatures — MCUs authenticate without TLS overhead |
| **Exactly-once delivery** | Binary ACK + LMDB-backed dedup per client, survives broker restart |
| **Persistence** | All messages written to LMDB (memory-mapped, crash-safe, 1 GB default) |
| **Prometheus metrics** | Live counters/gauges/histograms on HTTP :9091 |
| **Secure topic prefix** | Topics starting with `secure/` reject unsigned publishes at the broker |
| **Wildcard subscriptions** | RESP `SUBSCRIBE sensors/*` glob-style matching |
| **ESP32 native client** | Full Arduino/PlatformIO library in `esp32-metricmq/` |

---

## Architecture

@dot
digraph MetricMQ {
    rankdir=TB;
    bgcolor=transparent;
    fontname="Helvetica,Arial,sans-serif";
    node [fontname="Helvetica,Arial,sans-serif" fontsize=10 style="filled,rounded" shape=box];
    edge [fontname="Helvetica,Arial,sans-serif" fontsize=9 color="#555555"];

    subgraph cluster_clients {
        label="Clients";
        style=filled; fillcolor="#EEF2FF"; color="#6366F1"; fontsize=11;
        resp_client [label="RESP Client\n(redis-cli / C++ SDK)" fillcolor="#C7D2FE" color="#4F46E5"];
        bin_client  [label="Binary Client\n(BinaryPublisher)"    fillcolor="#C7D2FE" color="#4F46E5"];
        esp32       [label="ESP32 / MCU\n(MetricMQClient)"       fillcolor="#C7D2FE" color="#4F46E5"];
    }

    tcp [label="TCP :6379\nBroker::accept() loop\nmax 1000 connections"
         shape=box fillcolor="#FEF3C7" color="#D97706" fontsize=10];

    subgraph cluster_sessions {
        label="Session Layer  (one detached thread per client)";
        style=filled; fillcolor="#F0FDF4"; color="#16A34A"; fontsize=11;
        sess_resp [label="Session (RESP)\nRespParser\nno auth"                            fillcolor="#BBF7D0" color="#15803D"];
        sess_bin  [label="Session (Binary)\nBinaryProtocol::parse()\nEd25519 verify"      fillcolor="#BBF7D0" color="#15803D"];
    }

    subgraph cluster_core {
        label="Broker Core";
        style=filled; fillcolor="#FFF7ED"; color="#EA580C"; fontsize=11;
        broker [label="Broker\n(single global mutex\ntopic router)" shape=box fillcolor="#FED7AA" color="#C2410C"];
        ack    [label="ACK Tracker\nexactly-once\nunbounded set — OOM risk" fillcolor="#FED7AA" color="#C2410C"];
    }

    lmdb [label="LmdbStorage\nLMDB 1 GB map\nno TTL / no compaction"
          shape=cylinder fillcolor="#F3E8FF" color="#7C3AED" fontsize=10];

    prom [label="MetricsServer :9091\nPoco HTTP\nPrometheus text export"
          shape=box fillcolor="#FCE7F3" color="#BE185D" fontsize=10];

    resp_client -> tcp [label="RESP\n(no auth)"];
    bin_client  -> tcp [label="Binary"];
    esp32       -> tcp [label="Signed Binary\nACK"];

    tcp -> sess_resp;
    tcp -> sess_bin;

    sess_resp -> broker [label="publish / subscribe"];
    sess_bin  -> broker [label="publish / ACK"];

    broker -> ack  [label="dedup"];
    broker -> lmdb [label="persist all msgs"];
    broker -> prom [label="counters / gauges"];
}
@enddot

---

## Performance

Measured on a single developer machine (binary protocol, loopback):

| Benchmark | Result |
|---|---|
| Single-publisher throughput (10 KB msg) | **106,000 msg/s** |
| p99 publish latency | **68 µs** |
| LMDB sequential writes (1 KB) | **42,674 ops/s** |
| LMDB random reads | **1.56 M ops/s** |
| Binary size | **~328 KB** |

---

## Quick Start — Desktop (C++)

**1. Build**

~~~{.sh}
mkdir build && cd build
conan install .. --build=missing -s build_type=Release
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --parallel
~~~

**2. Run the broker**

~~~{.sh}
./MetricMQ          # TCP :6379 (broker) + HTTP :9091 (metrics)
~~~

**3. Publish via RESP (any redis-cli)**

~~~{.sh}
redis-cli -p 6379 PUBLISH sensors/temp "25.4"
~~~

**4. Publish a signed binary message (C++)**

~~~{.cpp}
auto [pk, sk] = metricmq::crypto::generate_keypair();
metricmq::crypto::get_global_keystore().add("device-1", pk);

metricmq::BinaryPublisher pub("127.0.0.1", 6379);
pub.setSigningKey("device-1", sk);
pub.sendSigned("sensors/temp", "25.4");
~~~

**5. Check live metrics**

~~~{.sh}
curl http://localhost:9091/metrics
~~~

---

## Quick Start — ESP32 (Arduino / PlatformIO)

Copy `esp32-metricmq/src/MetricMQ.h` and `MetricMQ.cpp` into your project.
Requires **arduino-esp32 v2.0.0+** (libsodium bundled in that SDK).

~~~{.cpp}
#include "MetricMQ.h"

MetricMQClient mqtt;

// Generate or load a stable keypair once (store in NVS in production)
uint8_t pk[32], sk[64];
MetricMQClient::generateKeypair(pk, sk);

void setup() {
    // Connect WiFi first, then:
    mqtt.setSigningKey("esp32-node-01", sk);
    mqtt.connect("192.168.1.100", 6379);
    mqtt.subscribe("cmd/device", [](const char* topic,
                                    const uint8_t* payload, size_t len,
                                    uint64_t seq) {
        // handle command
    });
}

void loop() {
    // IMPORTANT: auto-reconnect is NOT built-in — you must do this:
    if (!mqtt.isConnected()) {
        mqtt.connect("192.168.1.100", 6379);
    }
    mqtt.loop();          // drives keep-alive (60 s PING) and receive
    mqtt.publishSigned("sensors/temp", "25.4");
    delay(1000);
}
~~~

> **Warning:** The README claims "auto-reconnect with exponential backoff" but
> this is **not implemented** in the current code. `loop()` calls `resetConnection()`
> on disconnect and returns immediately — your sketch must detect `!isConnected()`
> and call `connect()` explicitly.

---

## Broker Hard Limits

| Parameter | Value | Notes |
|---|---|---|
| Default broker port | **6379** | Configurable via `Config::port` |
| Metrics port | **9091** | Hard-coded, not in `Config` |
| Max simultaneous connections | **1000** | `MAX_CONNECTIONS` constexpr in `broker.hpp` |
| Per-session input buffer cap | **16 MB** | Exceeding this drops the connection |
| Binary max topic length | **256 bytes** | Enforced in `BinaryProtocol::parse()` |
| Binary max payload length | **16 MB** | Enforced in `BinaryProtocol::parse()` |
| LMDB map size | **1 GB** | Hard-coded in `LmdbStorage.cpp` — no TTL, no compaction |
| Session idle timeout | **None** | Zombie sessions from crashed clients stay forever |
| Backpressure | **None** | Slow subscriber blocks the broker send path |
| RESP authentication | **None** | Any TCP client can publish/subscribe |

---

## ESP32 Client Hard Limits

| Parameter | Value | Notes |
|---|---|---|
| Max outbound frame | **1500 bytes** | topic + payload + header must fit; no fragmentation |
| Receive buffer | **2048 bytes** | Incoming frames larger than this are not handled |
| Max local verify keys | **4** (`MAX_VERIFY_KEYS`) | For verifying incoming signed messages on-device |
| Keep-alive PING interval | **60 seconds** | `keep_alive_ms_` in `MetricMQ.cpp` |
| Connection timeout | **180 seconds** | `keep_alive_ms_ * 3` |
| Exactly-once delivery | **Enabled by default** | Disable with `setExactlyOnce(false)` if RAM is tight |
| Auto-reconnect | **Not implemented** | Must poll `isConnected()` in your `loop()` |
| Client ID | **Auto** from MAC eFuse | `"esp32_" + HEX(chipId)` — unique per chip |
| libsodium requirement | **arduino-esp32 ≥ v2.0.0** | Bundled in the ESP-IDF SDK |

---

## Wire Protocol

Compact 16-byte binary header + payload:

| Offset | Size | Field |
|---|---|---|
| 0 | 1 | Protocol version (`0x01`) |
| 1 | 1 | Command — see metricmq::BinaryCommand |
| 2–3 | 2 | Topic length (big-endian, max 256) |
| 4–7 | 4 | Payload length (big-endian, max 16 MB) |
| 8–15 | 8 | Sequence number (big-endian, uint64) |
| 16+ | topic\_len | Topic UTF-8 string |
| 16+topic | payload\_len | Payload bytes |

For `CMD_SIGNED_PUBLISH` (0x10), a **64-byte Ed25519 signature** and **4-byte key-ID length**
prefix the payload. The signature covers raw bytes of `topic + payload` — **no separator**.
See metricmq::BinaryProtocol for the full frame layout.

---

## Security Model

MetricMQ uses **libsodium Ed25519** for message authentication:

1. Device calls `MetricMQClient::generateKeypair(pk, sk)` once; store `sk` in NVS/flash.
2. Register `pk` on the broker side via `TrustedKeyStore::add("device-id", pk)`.
3. `CMD_SIGNED_PUBLISH` frames carry a 64-byte signature over raw `topic + payload` bytes.
4. The broker verifies the signature before routing; unknown or disabled keys are rejected.
5. Private keys are erased from memory after use via `metricmq::crypto::secure_erase()`.
6. Topics prefixed with `secure/` enforce signed publish — unsigned frames are rejected.

**Important:** Ed25519 authenticates but does **not encrypt**. All payload bytes are visible
on the wire. Use TLS at the network layer if confidentiality is required.

**RESP protocol has zero authentication.** Any TCP client that reaches port 6379 can
publish to any topic and subscribe to any topic. Restrict access via firewall on
production deployments.

---

## Production Risks

These are known issues to address before deploying at scale:

| Risk | Severity | Detail |
|---|---|---|
| Unbounded ACK set | **High** | `unordered_set<uint64_t>` per client, never pruned — OOM on long-running brokers with many clients |
| No RESP authentication | **High** | Any device on the LAN can publish/subscribe; no credentials required |
| No session idle timeout | **Medium** | Crashed clients leave zombie threads alive indefinitely |
| LMDB fills to 1 GB | **Medium** | No TTL, no compaction — broker returns `MDB_MAP_FULL` when full |
| No backpressure | **Medium** | Slow subscriber causes broker `send()` to block under the global mutex |
| `crypto_demo.cpp` wrong signing format | **Medium** | Signs `topic + ":" + payload`; broker expects `topic + payload` — signatures will be rejected |
| Auto-reconnect not implemented on ESP32 | **Medium** | Devices silently stop publishing after WiFi glitch unless sketch retries `connect()` |
| Single global mutex | **Low** | All 1000 sessions contend on one lock — throughput degrades with many concurrent publishers |
| Hardcoded include paths in CMakeLists.txt | **Low** | Build fails on any machine that is not the original developer's |

---

## API Overview

| Class / Namespace | Purpose |
|---|---|
| metricmq::BinaryPublisher | Connect and send binary or Ed25519-signed messages |
| metricmq::BinarySubscriber | Subscribe with optional exactly-once acknowledgement |
| metricmq::BinaryProtocol | Stateless frame serialiser / parser |
| metricmq::crypto | Key generation, sign, verify, hex utilities, secure erase |
| metricmq::crypto::TrustedKeyStore | Thread-safe registry of trusted device public keys |
| metricmq::storage::LmdbStorage | LMDB persistence — messages and ACK records |
| metricmq::Metrics | Lock-free counters, gauges, histograms + Prometheus text export |
| metricmq::Logger | Singleton spdlog logger — rotating file + console sinks |
| metricmq::MetricsServer | Poco HTTP server for `/metrics` endpoint |
| metricmq::Publisher | High-level RESP publisher |
| metricmq::Subscriber | High-level RESP subscriber with wildcard support |

---

## Dependencies

| Library | Version | Purpose |
|---|---|---|
| libsodium | 1.0.19 | Ed25519 signing and verification |
| lmdb | 0.9.31 | Memory-mapped persistent storage |
| spdlog | 1.13.0 | Structured logging |
| fmt | 10.2.1 | String formatting |
| Poco | 1.13.3 | HTTP server for Prometheus metrics |
| Boost | 1.85.0 | Utilities |
| Catch2 | 3.6.0 | Unit testing |
| Google Benchmark | 1.8.3 | Latency / throughput benchmarks |

---

## Running Tests

Two tests run without a live broker; the rest require one running on `localhost:6379`.

| Executable | Broker needed | What it checks |
|---|---|---|
| `signed_publish_test` | No | Ed25519 keygen, sign, parse, tamper detection, unknown key rejection |
| `exactly_once_test` | Yes | ACK flow, no duplicates on reconnect, multi-client, wildcard ACK |
| `persistence_test` | Yes | Publishes 5 msgs then subscribes — expects LMDB replay |
| `test_metrics.ps1` | Yes | HTTP 200 from `:9091/metrics` (PowerShell) |

~~~{.sh}
# 1 — no broker needed
./signed_publish_test

# 2 — start broker first
./MetricMQ &
./exactly_once_test
./persistence_test
pwsh scripts/test_metrics.ps1
~~~

> **Note:** `exactly_once_test` and `persistence_test` exit `0` even on failure — check stdout
> for `❌` markers. Only `signed_publish_test` uses `assert()` and crashes on failure.
>
> Full test specs, expected output, failure diagnosis, and coverage gaps →
> \ref md_docs_TESTING "Testing Guide"

---

## Further Reading

- \ref md_docs_TECHNICAL "Technical Reference" — full API, wire format, defensive practices, known limitations
- \ref md_docs_TESTING "Testing Guide" — how to run every test and interpret results
- \ref md_docs_PLATFORMIO_GUIDE "PlatformIO Guide" — step-by-step setup for MetricMQ on ESP devices
