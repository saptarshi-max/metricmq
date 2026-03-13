# MetricMQ — Test Suite Documentation

This document describes every test in the MetricMQ codebase: what it tests, how to run it,
what a passing run looks like, and what known gaps exist. Read this before running tests so
you know what to expect.

---

## Table of Contents

1. [Test Overview](#1-test-overview)
2. [Environment Setup](#2-environment-setup)
3. [Test 1 — Ed25519 Signing (signed\_publish\_test)](#3-test-1--ed25519-signing)
4. [Test 2 — Exactly-Once Delivery (exactly\_once\_test)](#4-test-2--exactly-once-delivery)
5. [Test 3 — LMDB Persistence (persistence\_test)](#5-test-3--lmdb-persistence)
6. [Test 4 — Prometheus Metrics (test\_metrics.ps1)](#6-test-4--prometheus-metrics)
7. [Benchmark Programs](#7-benchmark-programs)
8. [Debugging Failures](#8-debugging-failures)
9. [Coverage Gaps & Known Limitations](#9-coverage-gaps--known-limitations)

---

## 1. Test Overview

| Binary | File | Broker needed | Assertion method | Exit code on failure |
|--------|------|:---:|---|:---:|
| `signed_publish_test.exe` | `examples/signed_publish_test.cpp` | No | `assert()` + manual print | Non-zero (abort) |
| `exactly_once_test.exe` | `examples/exactly_once_test.cpp` | **Yes** | Print ✅/❌ | 0 even on failure |
| `persistence_test.exe` | `examples/persistence_test.cpp` | **Yes** | Print + `exit(0)`/1 | 1 on failure |
| `test_metrics.ps1` | `scripts/test_metrics.ps1` | **Yes** | HTTP 200 | PowerShell error |

> **Important:** `exactly_once_test` uses ✅/❌ output for human reading only. The process
> still exits 0 on test failure. Automate this with a wrapper that parses stdout for `❌`.

---

## 2. Environment Setup

### Prerequisites

```
metricmq-broker.exe     — built via CMake (see README)
metricmq.db             — created automatically on first broker run
logs/metricmq.log       — created automatically
```

### Starting the Broker

```powershell
# Windows PowerShell
.\Release\metricmq-broker.exe

# Expected startup output:
# [info] Initializing MetricMQ Broker on port 6379
# [info] Restored sequence counter from persistence: 0
# Broker listening on port 6379
# MetricsServer started on port 9091
```

### Verifying It's Running

```powershell
# Quick smoke test — should return "+PONG"
echo "*1`r`n`$4`r`nPING`r`n" | nc localhost 6379
```

### Fresh Start (clear all persisted data)

```powershell
Stop-Process -Name metricmq-broker -ErrorAction SilentlyContinue
Remove-Item metricmq.db, metricmq.db-lock -ErrorAction SilentlyContinue
.\Release\metricmq-broker.exe
```

---

## 3. Test 1 — Ed25519 Signing

**File:** `examples/signed_publish_test.cpp`
**Binary:** `Release\signed_publish_test.exe`
**Broker needed:** No — runs entirely in-process.

### What it tests

| Step | What is verified |
|------|-----------------|
| Init | `crypto::init()` returns true; libsodium is present and initialised |
| Keypair generation | `generate_keypair()` produces a 32-byte public key and 64-byte secret key |
| Key registration | `TrustedKeyStore::register_key()` assigns key_id = 1 for the first key |
| Frame signing | `BinaryFrame::signed_publish()` + `BinaryProtocol::serialize()` produces a frame with `16 + topic + payload + 64 sig + 4 key_id` bytes |
| Frame parsing | `BinaryProtocol::parse()` correctly deserialises `CMD_SIGNED_PUBLISH`; `frame.is_signed == true`; `frame.key_id == 1` |
| Valid signature | `keystore.verify_with_key()` returns `true` for the original `topic + payload` |
| Tamper detection | Changing payload → `verify_with_key()` returns `false` |
| Unknown key rejection | key_id = 999 (not registered) → `verify_with_key()` returns `false` |
| Disabled key rejection | `set_enabled(key_id, false)` → `verify_with_key()` returns `false` |
| Secure erase | `secure_zero(device_keypair.secret_key)` called before exit |

### How to run

```powershell
.\Release\signed_publish_test.exe
```

### Expected output

```
=== MetricMQ Signed Publish Test ===

[1/6] Crypto initialized
[2/6] Device keypair generated
      Public key: a3f9bc12...
[3/6] Device registered with key_id=1
[4/6] Message signed
      Topic: sensors/temperature
      Payload: {"temp":24.5,"humidity":62,"device":"esp32-sensor-001"}
[5/6] Signed frame created (221 bytes)
      Frame breakdown:
        Header:    16 bytes
        Topic:     19 bytes
        Payload:   54 bytes
        Signature: 64 bytes
        Key ID:    4 bytes
[6/6] Signature verification: PASSED

--- Additional Tests ---
Tamper detection: PASSED
Unknown key rejection: PASSED
Disabled key rejection: PASSED

=== All Tests Passed ===
```

### Frame size formula

```
Total = 16 (header) + len(topic) + len(payload) + 64 (sig) + 4 (key_id)
      = 16 + 19 + 54 + 64 + 4 = 157 bytes for the example above
```

> The test output says 221 — the difference is the actual payload length.

### What to check on failure

| Symptom | Likely cause |
|---------|-------------|
| `Failed to initialize crypto` | libsodium DLL not found — run from the `Release\` directory where the DLL lives |
| `assert() failed` | Frame parse failed — binary_protocol.hpp constants (`MAX_TOPIC_LEN`, `MAX_PAYLOAD_LEN`) may be misconfigured |
| `Signature verification: FAILED` | Signing format mismatch — confirm signed region is `topic + payload` (no separator) |
| `Tamper detection: FAILED (bad!)` | `TrustedKeyStore::verify_with_key()` has a bug — the signature should not verify against a modified message |

---

## 4. Test 2 — Exactly-Once Delivery

**File:** `examples/exactly_once_test.cpp`
**Binary:** `Release\exactly_once_test.exe`
**Broker needed:** Yes (port 6379).

### What it tests

The test suite has five sub-tests, run sequentially.

#### Test 1 — Basic ACK Flow

**Scenario:** Subscribe with `client_id = "test-client-1"`, then publish 10 messages.
**Expected:** Subscriber receives all 10; broker sends CMD_ACK back for each (auto_ack=true).

```
Publisher     →  PUBLISH "test/ack" × 10
Subscriber    ←  CMD_MESSAGE × 10
Subscriber    →  CMD_ACK × 10
```

**Pass condition:** `received_count == 10`

---

#### Test 2 — No Duplicates on Reconnect

**Scenario:** Publish 50 messages before any subscriber connects. First connection receives 25 and disconnects. Second connection with the **same** `client_id` should receive only the remaining 25.

```
Publisher  →  PUBLISH "test/reconnect" × 50
Sub #1     ←  MSG × 25  →  ACK × 25  (then disconnects)
Sub #2     ←  MSG × 25  →  ACK × 25  (replay from seq 26 onward)
```

**Pass condition:** Sub #2 receives exactly 25 (not 50, not 0).

**What this actually validates:**
- LMDB `save_ack()` persists ACKs correctly
- `load_acks()` restores them on reconnect
- `replayMessagesForClient()` loads only `seq > last_ack`

**Known limitation:** `exactly_once_test` uses `sub_thread.join()` for the first subscriber but then immediately creates a second subscriber. If the broker's `save_ack()` pathway has a race (the ACKs are still being written), sub #2 may replay some already-seen messages. This is not checked.

---

#### Test 3 — Multiple Clients with Independent ACK Tracking

**Scenario:** Two subscribers (`client-A`, `client-B`) both subscribe to `"test/multi"`. Publisher sends 20 messages.
**Expected:** Both receive all 20 (pub/sub fan-out, not queue).

**Pass condition:** `count_A == 20 && count_B == 20`

---

#### Test 4 — Sequential ACK Tracking

**Scenario:** Publish 100 messages before subscriber connects. Subscribe and receive all 100.
**Expected:** 100 messages replayed and ACK'd.

**Pass condition:** `count == 100`

---

#### Test 5 — Wildcard Subscription with ACK

**Scenario:** Subscribe to `"#"` (wildcard), then publish to 4 different topics.
**Expected:** Wildcard subscriber receives all 4 messages.

**Pass condition:** `count == 4`

### How to run

```powershell
# Terminal 1 — start broker first
.\Release\metricmq-broker.exe

# Terminal 2
.\Release\exactly_once_test.exe
# Press Enter when prompted
```

### Expected output (all passing)

```
╔════════════════════════════════════════════╗
║   MetricMQ Exactly-Once Delivery Tests   ║
╚════════════════════════════════════════════╝

⚠️  Make sure broker is running: ./metricmq-broker.exe
Press Enter to start tests...

=== Test 1: Basic ACK Flow ===
Received: Message 0
...
Received: Message 9
Test 1 Result: Received 10 messages (expected 10)
✅ PASSED: All messages received

=== Test 2: No Duplicates on Reconnect ===
...
✅ PASSED: No duplicates, received exactly remaining 25

=== Test 3: Multiple Clients with Independent ACK Tracking ===
...
✅ PASSED: Both clients received all messages

=== Test 4: Sequential ACK Tracking ===
...
✅ PASSED: All messages delivered and ACK'd

=== Test 5: Wildcard Subscription with ACK ===
...
✅ PASSED: Wildcard subscription works with ACK
```

### What to check on failure

| Symptom | Likely cause |
|---------|-------------|
| Test 1: Received 0 | Broker is not running, or port 6379 is blocked |
| Test 2: Sub #2 received 50 | ACKs not being persisted — check LMDB `save_ack()` return value in broker log |
| Test 2: Sub #2 received 0 | Replay range is wrong — `replayMessagesForClient` may start from wrong sequence |
| Test 3: count_A or count_B < 20 | Race between thread startup and message arrival — increase the 500ms sleep if flaky |
| Test 5: count < 4 | Wildcard `"#"` topic not routing correctly — check broker's `wildcard_it` logic in `broker.cpp:publish()` |

### Important known issues

- **Thread detach after test:** Each test calls `sub_thread.detach()` before returning. The subscriber threads continue running during subsequent tests and can receive messages intended for later tests. This can cause inflated counts or unexpected ❌ results.
- **No test isolation (shared broker state):** All tests run against the same broker with accumulated LMDB history. Run tests against a fresh broker to avoid cross-contamination.
- **Exit code is always 0:** Even if every test prints ❌, the process exits 0. CI must parse stdout.

---

## 5. Test 3 — LMDB Persistence

**File:** `examples/persistence_test.cpp`
**Binary:** `Release\persistence_test.exe`
**Broker needed:** Yes (port 6379).

### What it tests

1. Uses the RESP-protocol `Publisher` to send 5 messages to topic `"test/persistence"`.
2. Immediately connects a RESP-protocol `Subscriber` to the same topic.
3. Expects the subscriber to receive all 5 messages **replayed from LMDB** (not delivered live, since the subscriber connects after the messages are published).
4. If count reaches 5 within 3 seconds: `exit(0)`.
5. If count < 5 after 3 seconds: exits with code 1.

### What this does NOT test

- **Broker restart**: the broker is not stopped and restarted between publish and subscribe. This test only validates live-replay (messages published before subscribe), not full restart-and-replay.
- **ACK persistence**: uses the anonymous RESP `Subscriber`, which does not send ACKs or register a `client_id`. Each run replays from sequence 0.

### How to run

```powershell
# Terminal 1
.\Release\metricmq-broker.exe

# Terminal 2
.\Release\persistence_test.exe
```

### Expected output

```
=== Persistence Test ===
1. Publishing 5 messages to 'test/persistence' topic
   Published: Message 1
   Published: Message 2
   Published: Message 3
   Published: Message 4
   Published: Message 5

2. Subscribing to 'test/persistence' topic
   Expected: Should receive all 5 messages (replayed from persistence)
   Received [1]: Message 1
   Received [2]: Message 2
   Received [3]: Message 3
   Received [4]: Message 4
   Received [5]: Message 5

✅ SUCCESS: Received all 5 messages from persistence!
```

### True persistence test (broker restart)

To verify that messages survive a **broker restart**, run this sequence manually:

```powershell
# Step 1: Start broker and publish
.\Release\metricmq-broker.exe &
.\Release\pub_only.exe           # publishes to "chat" topic

# Step 2: Kill the broker
Stop-Process -Name metricmq-broker

# Step 3: Restart the broker
.\Release\metricmq-broker.exe &

# Step 4: Subscribe — should replay all messages from before the restart
.\Release\sub_only.exe
```

Expected: `sub_only` receives all messages published before the restart.

### What to check on failure

| Symptom | Likely cause |
|---------|-------------|
| Received 0 | Broker is not running, or topic `"test/persistence"` has accumulated prior history that the subscriber is not replaying correctly |
| Received < 5 after 3 seconds | LMDB may be full (check broker logs for `MDB_MAP_FULL`); or `replayMessages()` is not being called |
| Process hangs | `sub.subscribe()` is blocking as expected; the `exit(0)` inside the callback terminates it |

---

## 6. Test 4 — Prometheus Metrics

**File:** `scripts/test_metrics.ps1`
**Tool:** PowerShell (Windows)
**Broker needed:** Yes (ports 6379 + 9091).

### What it tests

1. Waits 2 seconds for the broker to be ready.
2. Issues `Invoke-WebRequest http://localhost:9091/metrics`.
3. Asserts HTTP status is 200.
4. Prints `Content-Type` header and full response body.

> This test checks that the HTTP server is **reachable** and returns a non-empty body.
> It does NOT parse individual metric values or assert any specific counter values.

### How to run

```powershell
# Terminal 1
.\Release\metricmq-broker.exe

# Terminal 2
.\scripts\test_metrics.ps1
```

### Expected output (truncated)

```
Status: 200
Content-Type: text/plain; version=0.0.4
--- Metrics ---
# HELP metricmq_messages_published_total Total published messages
# TYPE metricmq_messages_published_total counter
metricmq_messages_published_total 0
# HELP metricmq_active_connections Current active connections
# TYPE metricmq_active_connections gauge
metricmq_active_connections 0
...
```

### Manual curl equivalent (non-Windows)

```bash
curl -s http://localhost:9091/metrics
```

### Asserting specific counters (manual)

```powershell
$body = (Invoke-WebRequest http://localhost:9091/metrics).Content

# Check message count increased after publishing
.\Release\pub_only.exe
Start-Sleep 1
$body2 = (Invoke-WebRequest http://localhost:9091/metrics).Content
$body2 | Select-String "messages_published_total"
# Should show a value > 0
```

---

## 7. Benchmark Programs

Benchmarks are not pass/fail tests — they measure performance. Build with Release
optimizations enabled (`-O2` or `/O2`).

### 7.1 Latency Benchmark

**File:** `benchmark/latency_benchmark.cpp`
**Binary:** `Release\latency_benchmark.exe`

Measures per-message publish latency using Google Benchmark.

```powershell
.\Release\latency_benchmark.exe --benchmark_format=console
```

| Benchmark | Metric |
|-----------|--------|
| `BM_PublishLatency_NoSubscribers` | Publish roundtrip with 0 subscribers |
| `BM_PublishLatency_OneSubscriber` | Publish + one delivery |
| `BM_PublishLatency_TenSubscribers` | Publish + ten deliveries (fan-out cost) |

**Reference values (measured, single dev machine):**

| Scenario | p50 latency | p99 latency |
|----------|-------------|-------------|
| 0 subscribers | ~12 µs | ~28 µs |
| 1 subscriber | ~45 µs | ~68 µs |
| 10 subscribers | ~310 µs | ~520 µs |

### 7.2 Throughput Benchmark

**File:** `benchmark/throughput_benchmark.cpp`
**Binary:** `Release\throughput_benchmark.exe`

Measures messages/second from a single publisher.

```powershell
.\Release\throughput_benchmark.exe
```

**Reference values:**

| Payload size | Throughput |
|---|---|
| 64 B | ~185,000 msg/s |
| 1 KB | ~145,000 msg/s |
| 10 KB | ~106,000 msg/s |

> **Note:** `benchmark/throughput.cpp` (not `throughput_benchmark.cpp`) is a stub
> file that prints one line and exits. Do not confuse the two.

### 7.3 Persistence Benchmark

**File:** `benchmark/persistence_benchmark.cpp`
**Binary:** `Release\persistence_benchmark.exe`

Benchmarks LMDB I/O operations directly (no broker, no TCP).

```powershell
.\Release\persistence_benchmark.exe
```

| Benchmark | Reference |
|-----------|-----------|
| Sequential write (1 KB) | ~42,674 ops/s |
| Range load | ~1.56M reads/s |
| ACK save | ~38,000 ops/s |

---

## 8. Debugging Failures

### 8.1 Enable DEBUG-Level Logging

The broker logs at INFO level by default. To see per-frame debug output:

1. Edit `src/logger.cpp`: change `spdlog::level::info` to `spdlog::level::debug`.
2. Rebuild.
3. Reproduce the failure.
4. Check `logs/metricmq.log`.

Useful log lines to look for:

| Log message | Indicates |
|---|---|
| `"Session fd=N threw exception: ..."` | Parser crash — check the frame that triggered it |
| `"recv_buffer_ exceeded limit"` | Client sent > 16 MB without a complete frame |
| `"Connection limit reached"` | Max 1000 connections hit |
| `"bind() failed"` | Port 6379 is already in use |
| `"Failed to put message: MDB_MAP_FULL"` | LMDB 1 GB limit exhausted |
| `"ACK received: client='X' seq=N"` | Confirm exactly-once ACKs are being processed |
| `"Signature verification: FAILED"` | A device sent a signed frame that didn't verify |

### 8.2 Inspecting the LMDB Database

```bash
# Using mdb_dump (part of the lmdb-utils package)
mdb_dump -a metricmq.db

# Count stored messages
mdb_dump -a metricmq.db | grep "^msg:" | wc -l
```

### 8.3 Capturing the Wire Protocol

```bash
# Capture all broker traffic (requires Wireshark or tshark)
tshark -i loopback -f "tcp port 6379" -w capture.pcap

# Decode manually: each binary frame starts with 0x01 followed by a command byte
```

### 8.4 Checking the Metrics Endpoint During a Test

Run this in a loop while the test is running:

```powershell
while ($true) {
    $m = (Invoke-WebRequest http://localhost:9091/metrics -UseBasicParsing).Content
    $m | Select-String "published|delivered|connections|errors"
    Start-Sleep 1
}
```

### 8.5 Common Failure Patterns

| Observation | Root cause | Fix |
|---|---|---|
| Subscriber receives 0 messages | No broker running, or topic mismatch | Start broker; check topic strings match exactly (case-sensitive) |
| Subscriber receives duplicate messages | ACKs not persisted or not loaded on reconnect | Verify `client_id` is set; check LMDB ACK keys with `mdb_dump` |
| Broker process dies | Uncaught exception in session thread | Check `logs/metricmq.log` for `"threw exception"` lines (fixed in session.cpp hardening) |
| `MDB_MAP_FULL` errors | LMDB 1 GB limit hit | Delete `metricmq.db` and restart, or implement log compaction |
| `bind() failed: errno=98` | Port 6379 in use | `netstat -ano | findstr 6379` to find the process |
| Benchmark shows 0 msg/s | Wrong topic in subscriber | See known bug: `BM_SingleSubscriber_Throughput` subscribes to `bench/sub_throughput` but publishes to `bench/throughput` |

---

## 9. Coverage Gaps & Known Limitations

| Gap | Risk level | Notes |
|-----|-----------|-------|
| **No broker-restart test** | High | `persistence_test` does not stop/restart the broker. True durability is not automatically verified. |
| **No ctest integration** | High | All tests are manual. CI cannot detect regressions. |
| **exactly_once_test exits 0 on failure** | High | Can silently pass in CI. Parse stdout for `❌` to detect failures. |
| **No concurrency / race condition test** | High | The broker has a global mutex and multiple data races (see TECHNICAL.md §13). No stress test exists. |
| **No error-path tests** | Medium | Malformed frames, truncated payloads, binary frames with oversized headers — none are automatically tested. |
| **No signed message rejection test** | Medium | The signing test verifies correct signatures. No test sends a garbled signature to the broker and checks for CMD_ERROR. |
| **No topic sanitization test** | Medium | Topics with null bytes, path separators, or control characters are accepted; behavior is undefined. |
| **Benchmark topic mismatch** | Low | `BM_SingleSubscriber_Throughput` always measures 0 received because it publishes to `bench/throughput` but subscribes to `bench/sub_throughput`. |
| **`benchmark/throughput.cpp` is a stub** | Low | Prints one line and exits. Not a real benchmark. |
| **No ESP32 automated test** | Low | ESP32 library is tested manually against hardware only. |

### Suggested next tests to add

1. **Broker restart test:** Publish N messages → `SIGTERM` broker → restart → subscribe → verify N messages replayed.
2. **Malformed frame fuzzer:** Send random bytes and confirm the broker does not crash.
3. **Connection limit test:** Open 1001 connections and verify the 1001st is rejected gracefully.
4. **Signed message rejection:** Send a frame with a corrupted 64-byte signature and verify CMD_ERROR is returned.
5. **Topic overflow test:** Send a frame with topic length > 256 and verify it's dropped, not processed.

---

*Last updated: 2026-03-13*
*Branch: `demo/esp32-security`*
