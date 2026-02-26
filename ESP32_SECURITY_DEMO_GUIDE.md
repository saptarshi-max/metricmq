# MetricMQ ESP32-S3 Security Demo — Complete Guide

## Overview

This guide walks you through demonstrating MetricMQ's full feature set on **2 ESP32-S3 N16R8 boards**, including:

- **Ed25519 message signing** — every message carries a 64-byte signature
- **Secure topics** — the broker rejects unsigned publishes to `secure/` topics
- **End-to-end verification** — subscriber verifies signatures locally (not just trusting the broker)
- **Binary protocol** — 16-byte header, 40% smaller than RESP
- **Exactly-once delivery** — duplicate detection built in
- **Auto-reconnect & keep-alive**

Both `master` and `demo/esp32-security` branches point to **the same commit** (`d1f7df2`). Everything works from either branch.

---

## Architecture

```
  ┌────────────────────────┐                       ┌────────────────────────┐
  │   ESP32-S3 Board #1    │        WiFi           │   ESP32-S3 Board #2    │
  │   SIGNED PUBLISHER     │                       │   VERIFYING SUBSCRIBER │
  │                        │                       │                        │
  │  Ed25519 signs every   │     ┌──────────┐      │  Verifies signatures   │
  │  message with secret   │────►│   PC     │◄─────│  locally with pub key  │
  │  key before sending    │     │  Broker  │      │                        │
  │                        │     │  :6379   │      │  RGB LED feedback:     │
  │  Publishes to:         │     │  :9091   │      │    ✓ Verified = blink  │
  │   secure/sensors/*     │     └──────────┘      │    ✗ Failed = rapid    │
  │   sensors/esp32/*      │         │             │                        │
  │                        │    Prometheus          │  Subscribes to:        │
  │  Shows sign timing     │    /metrics            │   secure/#, sensors/#  │
  │  in Serial Monitor     │                       │   demo/#               │
  └────────────────────────┘                       └────────────────────────┘
```

### Security Model
```
Board #1 (Publisher)                    Broker                    Board #2 (Subscriber)
     │                                   │                             │
     │ SIGNED_PUBLISH(0x10)              │                             │
     │ [header][topic][payload]          │                             │
     │ [64-byte Ed25519 sig][key_id]     │                             │
     │──────────────────────────────────►│                             │
     │                                   │ 1. Verify signature         │
     │                                   │ 2. Check key_id registered  │
     │                                   │ 3. Check topic authorization│
     │                                   │ 4. Forward as SIGNED_MESSAGE│
     │                                   │──────────────────────────►  │
     │                                   │                    5. LOCAL │
     │                                   │                    verify   │
     │                                   │                    with     │
     │                                   │                    pub key  │
```

---

## What You Need

| Item | Details |
|------|---------|
| 2× ESP32-S3-DevKitC-1-N16R8 | 16 MB Flash, 8 MB PSRAM |
| 2× USB-C cables | For programming + Serial Monitor |
| Windows PC | Same WiFi network as ESP32s |
| Arduino IDE 2.x | With ESP32 board package installed |
| MetricMQ repo | `C:\Users\Sapta\Documents\Projects\MetricMQ` |

---

## Phase 1: Build Everything (PC)

### 1.1 Find Your PC's IP

```powershell
ipconfig | Select-String "IPv4"
```
Note it (e.g., `192.168.1.100`).

### 1.2 Build the Broker + Keygen Tool

```powershell
cd C:\Users\Sapta\Documents\Projects\MetricMQ

# Configure (if needed)
cmake --preset conan-release

# Build broker + keygen
cmake --build build --config Release --target metricmq-broker
cmake --build build --config Release --target metricmq-keygen
```

### 1.3 Generate Ed25519 Keys for Board #1

```powershell
.\build\Release\metricmq-keygen.exe "sensor_node_1" "secure/sensors/*"
```

This outputs:
1. **C arrays** — paste into `SecurePublisher.ino` (the secret key) and `SecureSubscriber.ino` (the public key)
2. **Broker registration code** — add to your broker startup
3. **A `.h` file** — `sensor_node_1_keys.h` saved in current directory

**Save the output!** You need the secret key for Board #1 and the public key for Board #2 + broker.

### 1.4 Register the Key in the Broker

Open `src/main.cpp` and add the public key registration before the broker starts. The keygen tool prints the exact code to paste, e.g.:

```cpp
// In main(), after creating the broker:
uint8_t sensor_node_1_pk[32] = { /* keygen output */ };
broker.get_keystore().register_key(1, sensor_node_1_pk, {"secure/sensors/*"});
```

Then rebuild:
```powershell
cmake --build build --config Release --target metricmq-broker
```

### 1.5 Start the Broker

```powershell
.\build\Release\metricmq-broker.exe
```

You should see:
```
MetricMQ broker starting on port 6379
Prometheus metrics on port 9091
```

---

## Phase 2: Arduino IDE Setup

### 2.1 Install ESP32 Board Package

1. **File → Preferences → Additional Board Manager URLs**, add:
   ```
   https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
   ```
2. **Tools → Board → Board Manager**, search `esp32`, install **esp32 by Espressif Systems** (v2.0.14+)

### 2.2 Install MetricMQ Library

Copy the ESP32 library folder into Arduino's libraries directory:

```powershell
# Copy the library
Copy-Item -Recurse "C:\Users\Sapta\Documents\Projects\MetricMQ\esp32-metricmq" `
    "$env:USERPROFILE\Documents\Arduino\libraries\MetricMQ"
```

Or in Arduino IDE: **Sketch → Include Library → Add .ZIP Library** → select the `esp32-metricmq` folder.

### 2.3 Board Settings

For each board:
- **Board**: `ESP32S3 Dev Module`
- **USB CDC On Boot**: `Enabled` (for Serial Monitor over USB)
- **PSRAM**: `OPI PSRAM` (N16R8 has 8 MB PSRAM)
- **Flash Size**: `16MB (128Mb)`
- **Partition Scheme**: `Default 4MB with spiffs` (or `16M Flash (3MB APP/9.9MB FATFS)`)
- **Upload Speed**: `921600`

---

## Phase 3: Flash Board #1 — Signed Publisher

### 3.1 Open the Sketch

**File → Examples → MetricMQ → SecurePublisher** (or open `esp32-metricmq/examples/SecurePublisher/SecurePublisher.ino` directly)

### 3.2 Configure

Edit these lines at the top of the sketch:

```cpp
const char* WIFI_SSID      = "YourWiFiSSID";      // ← Your WiFi
const char* WIFI_PASSWORD   = "YourWiFiPassword";  // ← Your WiFi password
const char* BROKER_HOST     = "192.168.1.100";     // ← Your PC's IP
const uint16_t BROKER_PORT  = 6379;

const uint32_t DEVICE_KEY_ID = 1;                  // ← From keygen output

// Paste the DEVICE_SECRET_KEY[64] array from keygen output
const uint8_t DEVICE_SECRET_KEY[64] = {
    // ... keygen output ...
};
```

### 3.3 Upload & Monitor

1. Connect Board #1 via USB
2. Select the correct COM port in Arduino IDE
3. **Upload**
4. Open **Serial Monitor** at 115200 baud

You should see:
```
╔══════════════════════════════════════════════════╗
║  MetricMQ SECURITY Demo — SIGNED PUBLISHER       ║
║  Ed25519 Signatures | Secure Topics | ESP32-S3   ║
╚══════════════════════════════════════════════════╝

PSRAM: 8388608 bytes available
Connecting to WiFi 'YourWiFi'... Connected! IP: 192.168.1.42
Connecting to broker at 192.168.1.100:6379...
>>> CONNECTED (binary protocol v1)
>>> Ed25519 signing: ENABLED
>>> Key ID: 1

[SIGNED] #1 -> secure/sensors/temperature (85 bytes, 1247us sign+send)
[UNSIGNED] #1 -> sensors/esp32/telemetry (62 bytes, 89us)
[SIGNED] #2 -> secure/sensors/temperature (85 bytes, 1195us sign+send)
```

**Key things to watch:**
- Signing overhead is ~1ms (Ed25519 is fast on ESP32-S3)
- At the **30-second mark**, the demo deliberately tries an unsigned publish to `secure/sensors/temperature` — the broker will reject it

---

## Phase 4: Flash Board #2 — Verifying Subscriber

### 4.1 Open the Sketch

Open `esp32-metricmq/examples/SecureSubscriber/SecureSubscriber.ino`

### 4.2 Configure

```cpp
const char* WIFI_SSID      = "YourWiFiSSID";      // Same WiFi
const char* WIFI_PASSWORD   = "YourWiFiPassword";
const char* BROKER_HOST     = "192.168.1.100";     // Same broker

// Paste Board #1's PUBLIC key here (from keygen output)
const uint32_t PUBLISHER_KEY_ID = 1;
const uint8_t PUBLISHER_PUBLIC_KEY[32] = {
    // ... keygen output (PUBLIC key, NOT secret!) ...
};
```

### 4.3 Upload & Monitor

1. Connect Board #2 via USB (use a different COM port)
2. Upload and open Serial Monitor at 115200

You should see:
```
╔══════════════════════════════════════════════════╗
║  MetricMQ SECURITY Demo — VERIFYING SUBSCRIBER   ║
║  Ed25519 Verification | Split Trust | ESP32-S3   ║
╚══════════════════════════════════════════════════╝

>>> Ed25519 verification: ENABLED (local)
>>> Registered publisher key_id: 1
>>> CONNECTED (binary protocol v1)
>>> Subscribed: secure/#
>>> Subscribed: sensors/#

[SIGNED ✓ ] key_id=1  secure/sensors/temperature -> {"temp":42.3,"touch":112,...}
[MSG] sensors/esp32/telemetry -> {"temp":42.3,"heap":280000,...}
[SIGNED ✓ ] key_id=1  secure/sensors/temperature -> {"temp":42.5,...}
```

**Key things to watch:**
- `[SIGNED ✓]` = signature verified locally using Board #1's public key
- `[SIGNED ✗]` = verification failed (rapid LED blink — would only happen with tampered messages)
- `[MSG]` = normal unsigned message (no verification needed)
- Per-topic stats every 10 seconds show verification success rate

---

## Phase 5: Desktop Monitoring (Optional)

### 5.1 Watch with Desktop Subscriber

In a second terminal:
```powershell
.\build\Release\binary_sub_only.exe
```
This will show all messages flowing through the broker.

### 5.2 Prometheus Metrics

Open `http://localhost:9091/metrics` to see:
- `metricmq_messages_total` — total messages routed
- `metricmq_clients_connected` — currently connected clients
- `metricmq_bytes_in/out` — bandwidth usage

---

## What the Demo Proves

| Feature | How It's Demonstrated |
|---------|----------------------|
| **Ed25519 signing** | Board #1 signs every `secure/` message; timing shown in serial |
| **Broker enforcement** | Unsigned publish to `secure/` rejected at 30s mark |
| **End-to-end verification** | Board #2 verifies signatures locally, doesn't just trust broker |
| **Topic authorization** | Key is scoped to `secure/sensors/*` — other secure topics would be rejected |
| **Binary protocol** | Both boards use 16-byte binary header (not RESP) |
| **Exactly-once** | Sequence tracking + dedup on subscriber |
| **Auto-reconnect** | Unplug/replug a board — it reconnects and resumes |
| **ESP32 performance** | ~1ms signing overhead, ~280KB free heap with crypto running |
| **PSRAM detection** | Reported at startup — N16R8 has 8MB |

---

## Troubleshooting

| Problem | Solution |
|---------|----------|
| `Connection failed` | Check broker is running; check IP address; check firewall allows port 6379 |
| `WiFi timeout` | Check SSID/password; move boards closer to router |
| `SIGNED_PUBLISH rejected` | Key not registered in broker, or wrong key_id |
| `[SIGNED ✗] FAILED` | Wrong public key on Board #2 — must match Board #1's keypair |
| No messages on Board #2 | Check both boards on same broker; check subscriptions match topics |
| `Board not found` in IDE | Install ESP32 board package; select `ESP32S3 Dev Module` |
| Upload fails | Hold BOOT button while uploading; try lower upload speed |
| No serial output | Enable `USB CDC On Boot` in board settings |

### Windows Firewall

If Board #1/Board #2 can't connect, allow the broker through Windows Firewall:

```powershell
New-NetFirewallRule -DisplayName "MetricMQ Broker" -Direction Inbound -LocalPort 6379 -Protocol TCP -Action Allow
```

---

## File Locations

| File | Purpose |
|------|---------|
| `esp32-metricmq/src/MetricMQ.h` | ESP32 client library header |
| `esp32-metricmq/src/MetricMQ.cpp` | ESP32 client implementation (protocol + Ed25519) |
| `esp32-metricmq/examples/SecurePublisher/` | Board #1 sketch (signed publisher) |
| `esp32-metricmq/examples/SecureSubscriber/` | Board #2 sketch (verifying subscriber) |
| `esp32-metricmq/examples/DemoPublisher/` | Simpler demo (no signing) |
| `esp32-metricmq/examples/DemoSubscriber/` | Simpler demo (no signing) |
| `tools/keygen.cpp` | Ed25519 key generator |
| `src/main.cpp` | Broker entry point (add key registration here) |
| `src/session.cpp` | Broker session handler (secure topic enforcement) |

---

## Quick Start Checklist

- [ ] Build broker: `cmake --build build --config Release --target metricmq-broker`
- [ ] Build keygen: `cmake --build build --config Release --target metricmq-keygen`
- [ ] Generate keys: `.\build\Release\metricmq-keygen.exe "sensor_node_1" "secure/sensors/*"`
- [ ] Register public key in broker `src/main.cpp` and rebuild
- [ ] Start broker: `.\build\Release\metricmq-broker.exe`
- [ ] Copy `esp32-metricmq` to Arduino libraries folder
- [ ] Flash Board #1 with SecurePublisher (secret key + WiFi config)
- [ ] Flash Board #2 with SecureSubscriber (public key + WiFi config)
- [ ] Open Serial Monitors on both boards (115200 baud)
- [ ] Watch signed messages flow and verify!
