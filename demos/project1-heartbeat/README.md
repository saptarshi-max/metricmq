# Project 1 — Heartbeat Monitor

**Feature demonstrated:** Basic pub/sub, binary protocol auto-detection, Prometheus metrics, reconnect detection.

## What It Does

- **Board A** (Publisher) sends a heartbeat every 3 seconds with uptime, free heap, WiFi RSSI, and PSRAM info.
- **Board B** (Monitor) subscribes and prints each heartbeat. If Board A goes silent for 10+ seconds, it raises an alarm.

## Hardware

- 2x ESP32-S3 N16R8 (16 MB Flash, 8 MB PSRAM)
- 2x USB-C data cables
- Laptop running the MetricMQ broker on the same WiFi network

## Setup

### 1. Configure WiFi & Broker IP

Edit **both** `board-a/src/main.cpp` and `board-b/src/main.cpp`:

```cpp
const char* WIFI_SSID     = "YourWiFi";        // ← your WiFi SSID
const char* WIFI_PASSWORD = "YourPassword";     // ← your WiFi password
const char* BROKER_HOST   = "192.168.29.168";   // ← your laptop's IP
```

Find your laptop's IP:
```powershell
# Windows
ipconfig | Select-String "IPv4"

# Linux / macOS
hostname -I
```

### 2. Start the Broker

```powershell
cd MetricMQ\build\Release
.\metricmq-broker.exe
```

Leave this terminal open.

### 3. Flash Board A

```bash
cd demos/project1-heartbeat/board-a
pio run --target upload
pio device monitor
```

### 4. Flash Board B

Plug Board B into a **different USB port**. Open a new terminal:

```bash
cd demos/project1-heartbeat/board-b

# List ports to find Board B
pio device list

# Upload and monitor (replace COM4 with your actual port)
pio run --target upload --upload-port COM4
pio device monitor --port COM4
```

## What to Expect

### Board A — Serial Output

```
╔══════════════════════════════════════╗
║  Project 1 — Heartbeat Publisher     ║
║  Board A · ESP32-S3 N16R8            ║
╚══════════════════════════════════════╝

PSRAM: 8388608 bytes available
Connected! IP: 192.168.1.42
Connected to MetricMQ broker

[beat #1] heap=280320 rssi=-42
[beat #2] heap=280120 rssi=-43
[beat #3] heap=280320 rssi=-41
```

### Board B — Serial Output

```
╔══════════════════════════════════════╗
║  Project 1 — Heartbeat Monitor       ║
║  Board B · ESP32-S3 N16R8            ║
╚══════════════════════════════════════╝

Subscribed to devices/board-a/heartbeat
Waiting for heartbeats (alarm after 10s of silence)...

[seq=1 #1] devices/board-a/heartbeat: {"beat":1,"uptime_s":3,"heap":280320,"rssi":-42,"psram_free":8380000}
[seq=2 #2] devices/board-a/heartbeat: {"beat":2,"uptime_s":6,"heap":280120,"rssi":-43,"psram_free":8380000}
```

### The Disconnect Test

1. **Unplug Board A** — after ~10 seconds, Board B prints:
   ```
   !!! ALARM: Board A heartbeat lost for 10+ s !!!
   ```
2. **Plug Board A back in** — it reconnects, resumes heartbeats. Board B sees messages again and the alarm clears.

## How to Monitor

### Prometheus Metrics (laptop)

```bash
curl -s http://localhost:9091/metrics | grep metricmq_messages
```

You should see `metricmq_messages_published_total` incrementing.

### redis-cli (laptop — zero extra tools)

```bash
redis-cli -p 6379 SUBSCRIBE devices/board-a/heartbeat
```

You'll see the same heartbeats in RESP format — both protocols running simultaneously on the same broker port.

## Troubleshooting

| Problem | Fix |
|---------|-----|
| No serial output | Make sure `ARDUINO_USB_CDC_ON_BOOT=1` is in build_flags |
| `Connection refused` | Check broker is running; check firewall allows port 6379 |
| Board B never receives | Verify both boards use the same `BROKER_HOST` and are on the same WiFi |
| Upload fails | Hold BOOT button on ESP32-S3 during upload; check USB cable is a data cable |
