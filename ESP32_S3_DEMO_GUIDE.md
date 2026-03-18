# MetricMQ ESP32-S3 N16R8 Two-Board Demo Guide

## Important Note on Branches

Both `master` and `demo/esp32-security` currently point to **the same commit** (`d1f7df2`). All features — including Ed25519 security — already exist on both branches. You can run everything from a single branch.

---

## Hardware Setup

```
  ┌──────────────────────┐                     ┌──────────────────────┐
  │   ESP32-S3 Board #1  │                     │   ESP32-S3 Board #2  │
  │   N16R8 (PUBLISHER)  │     WiFi            │   N16R8 (SUBSCRIBER) │
  │                      │ ──────────►         │                      │
  │  Simulates sensors   │            ┌──────┐ │  Receives + displays │
  │  Publishes JSON      │ ──────────►│  PC  │ │  LED blinks on msg   │
  │  LED blinks on pub   │            │Broker│◄──  Dedup verification │
  │                      │            │:6379 │ │  Per-topic stats     │
  │  USB → Serial Monitor│            │:9091 │ │  USB → Serial Monitor│
  └──────────────────────┘            └──────┘ └──────────────────────┘
                                         │
                                    Prometheus
                                    /metrics
                                    (+ Desktop security demo)
```

### What You Need
- 2x ESP32-S3 N16R8 boards
- 2x USB-C cables
- Your PC (Windows) on the same WiFi network
- Arduino IDE 2.x installed

---

## Step 0: Find Your PC's IP Address

Open PowerShell and run:
```powershell
ipconfig | Select-String "IPv4"
```
Note your local IP (e.g., `192.168.1.100`). You'll use this as `BROKER_HOST` in both sketches.

---

## Step 1: Build and Start the Broker (PC)

```powershell
cd C:\Users\Sapta\Documents\Projects\MetricMQ

# Build the broker (if not already built)
cmake --build build --config Release --target metricmq-broker

# Run the broker
.\build\Release\metricmq-broker.exe
```

The broker will:
- Listen on port **6379** (binary + RESP auto-detect)
- Expose Prometheus metrics on port **9091** (`http://localhost:9091/metrics`)
- Log all connections to console + rotating file

**Keep this terminal open** throughout the demo.

---

## Step 2: Arduino IDE Setup (One-Time)

### Install ESP32-S3 Board Support
1. **File → Preferences → Additional Board Manager URLs**, add:
   ```
   https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
   ```
2. **Tools → Board → Boards Manager** → search `esp32` → install **esp32 by Espressif Systems**

### Install the MetricMQ Library
1. Copy the `esp32-metricmq` folder:
   ```
   C:\Users\Sapta\Documents\Projects\MetricMQ\esp32-metricmq\
   ```
   into your Arduino libraries folder:
   ```
   C:\Users\Sapta\Documents\Arduino\libraries\MetricMQ\
   ```
   So the structure becomes:
   ```
   Arduino/libraries/MetricMQ/
   ├── library.properties
   ├── src/
   │   ├── MetricMQ.h
   │   └── MetricMQ.cpp
   └── examples/
       ├── DemoPublisher/
       ├── DemoSubscriber/
       ├── SimplePublish/
       ├── SimpleSubscribe/
       └── DHT22Sensor/
   ```

### Board Configuration (for both boards)
- **Board**: `ESP32S3 Dev Module`
- **USB CDC On Boot**: `Enabled`
- **PSRAM**: `OPI PSRAM` (to enable the 8MB PSRAM)
- **Flash Size**: `16MB (128Mb)`
- **Partition Scheme**: `Default 4MB with spiffs` (or `16M Flash (3MB APP/9.9MB FATFS)`)
- **Upload Speed**: `921600`
- **Port**: Select the COM port for the connected board

---

## Step 3: Flash Board #1 — Publisher

1. Connect **Board #1** via USB
2. Open `DemoPublisher.ino` from:
   ```
   File → Examples → MetricMQ → DemoPublisher
   ```
   Or open directly:
   ```
   C:\Users\Sapta\Documents\Projects\MetricMQ\esp32-metricmq\examples\DemoPublisher\DemoPublisher.ino
   ```
3. **Update these lines** at the top:
   ```cpp
   const char* WIFI_SSID     = "YourActualSSID";
   const char* WIFI_PASSWORD  = "YourActualPassword";
   const char* BROKER_HOST    = "192.168.1.100";  // Your PC IP from Step 0
   ```
4. **Upload** (Ctrl+U)
5. Open **Serial Monitor** (115200 baud) — you'll see:
   ```
   ╔══════════════════════════════════════════╗
   ║  MetricMQ Demo - ESP32-S3 PUBLISHER      ║
   ╚══════════════════════════════════════════╝
   Connecting to WiFi... Connected!
   >>> CONNECTED to broker (binary protocol v1)
   >>> Exactly-once delivery: ENABLED
   --- Phase 1: Sensor Telemetry ---
   [    5s] PUB #1 -> sensors/esp32/telemetry (87 bytes, 1234us)
   ```

---

## Step 4: Flash Board #2 — Subscriber

1. Connect **Board #2** via USB
2. Open `DemoSubscriber.ino`
3. Update WiFi credentials and `BROKER_HOST` (same values as Board #1)
4. **Upload** and open Serial Monitor
5. You'll see messages arriving from Board #1:
   ```
   ╔══════════════════════════════════════════╗
   ║  MetricMQ Demo - ESP32-S3 SUBSCRIBER     ║
   ╚══════════════════════════════════════════╝
   >>> Subscribed: sensors/esp32/# (wildcard)
   READY - Waiting for messages...

   ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
   MSG #1
     Topic:    sensors/esp32/telemetry
     Payload:  {"temp":22.3,"hum":54.2,"rssi":-45,...}
   ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
   ```

---

## Step 5: Demo Phases (Automatic)

The publisher automatically progresses through 3 phases:

| Time | Phase | What Happens | What to Show |
|------|-------|-------------|-------------|
| 0-60s | **Telemetry** | Publish every 5s to `sensors/esp32/telemetry` | Normal IoT sensor data flow |
| 60-90s | **Burst** | Publish every 1s to `sensors/esp32/burst` | Higher throughput, binary protocol efficiency |
| 90s+ | **Multi-Topic** | Publish to 3 topics every 5s | Wildcard subscription, fan-out routing |

**Watch the subscriber**: it catches ALL messages via `sensors/esp32/#` wildcard and reports per-topic stats.

---

## Step 6: Desktop Security Demo (Parallel)

While the ESP32 boards are running, open a **new terminal** on your PC to demonstrate Ed25519 signed publishing:

```powershell
# Run the signed publish test (proves Ed25519 crypto works)
.\build\Release\signed_publish_test.exe

# Run the crypto demo
.\build\Release\crypto_demo.exe
```

This demonstrates:
- **Ed25519 keypair generation** using libsodium
- **Signed PUBLISH** with 64-byte signature embedded in binary frames
- **Tamper detection** — modified payloads are rejected
- **Unknown key rejection** — messages from unregistered keys fail
- **Disabled key rejection** — revoked keys are blocked
- **Topic-scoped permissions** — keys only work for authorized topics

### Cross-Protocol Demo (Optional)
```powershell
# In terminal 1 — binary subscriber
.\build\Release\binary_sub_only.exe

# In terminal 2 — RESP publisher (different protocol!)
.\build\Release\pub_only.exe
```
This shows the broker auto-detecting protocols and routing between RESP and Binary clients.

---

## Step 7: Prometheus Metrics (Optional)

While everything is running, open a browser to:
```
http://localhost:9091/metrics
```

You'll see Prometheus-formatted metrics showing message counts, throughput, and latency from all connected clients (both ESP32 boards + any desktop clients).

---

## Step 8: Resilience Testing

### Test Exactly-Once Delivery
1. While both boards are running, **unplug Board #2** (subscriber) for 10 seconds
2. Plug it back in — it reconnects and re-subscribes
3. Check the subscriber stats: `Duplicates: 0 (exactly-once working!)`

### Test Broker Restart
1. Press Ctrl+C on the broker terminal (graceful shutdown)
2. Wait 5 seconds, restart the broker
3. Both ESP32 boards auto-reconnect
4. LMDB persistence ensures no ACK state is lost

### Test WiFi Resilience
1. Briefly disable/enable your WiFi router
2. Both boards detect disconnection and auto-reconnect
3. Publishing resumes automatically

---

## What Each Feature Demonstrates

| Feature | Where to See It |
|---------|----------------|
| **Binary Protocol** | All ESP32 communication (40% smaller than RESP) |
| **Exactly-Once Delivery** | Subscriber reports 0 duplicates |
| **Wildcard Subscriptions** | `sensors/esp32/#` catches all subtopics |
| **Cross-Protocol Routing** | Desktop RESP ↔ ESP32 Binary via same broker |
| **Ed25519 Signed Publish** | `signed_publish_test.exe` on desktop |
| **LMDB Persistence** | Broker restart preserves ACK state |
| **Prometheus Metrics** | `http://localhost:9091/metrics` |
| **Graceful Shutdown** | Ctrl+C on broker, clean disconnect |
| **Auto-Reconnect** | Unplug/replug either board |
| **PSRAM Utilization** | Stats show 8MB PSRAM availability |

---

## Troubleshooting

| Problem | Solution |
|---------|---------|
| ESP32 won't connect to WiFi | Double-check SSID/password, ensure 2.4GHz (not 5GHz only) |
| Can't connect to broker | Check PC firewall allows port 6379 inbound; verify IP with `ipconfig` |
| No messages on subscriber | Ensure both boards use the same `BROKER_HOST`; check broker console for connections |
| Board not detected in Arduino | Install CP210x or CH340 USB driver; try different USB cable |
| Upload fails | Hold BOOT button while clicking Upload; release after "Connecting..." |
| PSRAM not detected | Set **PSRAM: OPI PSRAM** in Tools menu |
