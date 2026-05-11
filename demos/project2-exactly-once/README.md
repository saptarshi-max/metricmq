# Project 2 — Exactly-Once Sensor Logger

**Feature demonstrated:** Sequence IDs, ACK tracking, disconnect-and-replay with zero gaps and zero duplicates, LMDB persistence.

## What It Does

- **Board A** (Sensor) publishes a simulated temperature reading every 2 seconds with an incrementing count.
- **Board B** (Logger) subscribes and logs every reading with its broker-assigned sequence number. It detects gaps and duplicates automatically.

The key moment: **you unplug Board B, wait 20 seconds, plug it back in** — and it picks up exactly where it left off. No missed readings, no duplicates.

## Hardware

- 2x ESP32-S3 N16R8 (16 MB Flash, 8 MB PSRAM)
- 2x USB-C data cables
- Laptop running the MetricMQ broker on the same WiFi network

## Setup

### 1. Configure WiFi & Broker IP

Edit **both** `board-a/src/main.cpp` and `board-b/src/main.cpp`:

```cpp
const char* WIFI_SSID     = "YourWiFi";
const char* WIFI_PASSWORD = "YourPassword";
const char* BROKER_HOST   = "192.168.29.168";   // ← your laptop's IP
```

### 2. Start the Broker

```powershell
cd MetricMQ\build\Release
.\metricmq-broker.exe
```

### 3. Flash Board A

```bash
cd demos/project2-exactly-once/board-a
pio run --target upload
pio device monitor
```

### 4. Flash Board B

Open a new terminal. Plug Board B into a different USB port:

```bash
cd demos/project2-exactly-once/board-b
pio device list                             # find Board B's port
pio run --target upload --upload-port COM4   # replace COM4
pio device monitor --port COM4
```

## The Experiment

This is the whole point of this project. Follow these steps exactly:

### Step 1 — Let it run

Both boards are printing. Board B should show:
```
[seq=1 total=1] {"n":1,"temp":25.3,"heap":280320}
[seq=2 total=2] {"n":2,"temp":24.8,"heap":280120}
[seq=3 total=3] {"n":3,"temp":25.1,"heap":280320}
...
```
Sequence numbers increment cleanly. No gaps, no duplicates.

### Step 2 — Unplug Board B

Physically disconnect Board B's USB cable. Leave Board A running — it keeps publishing every 2 seconds. The broker persists these messages to LMDB.

### Step 3 — Wait 20 seconds

Board A will publish ~10 more readings while Board B is offline.

### Step 4 — Plug Board B back in

Reconnect Board B. Reopen the serial monitor:
```bash
pio device monitor --port COM4
```

### Step 5 — Watch the replay

Board B reconnects and the broker replays every message since the last ACK:
```
[!] Reconnecting — expecting replay from last ACK...
[seq=16 total=16] {"n":16,"temp":24.2,"heap":279800}
[seq=17 total=17] {"n":17,"temp":24.8,"heap":280120}
[seq=18 total=18] {"n":18,"temp":25.1,"heap":280320}
...continues with no gaps...
```

**Zero gaps. Zero duplicates.** The broker knew which sequence Board B last acknowledged (because `client_id = "logger-B"` is tracked in LMDB) and replayed everything since.

## What to Expect

### Board A — Serial Output

```
╔══════════════════════════════════════╗
║  Project 2 — Sensor Publisher        ║
║  Board A · Exactly-Once Demo         ║
╚══════════════════════════════════════╝

[#1] 25.3°C  (heap=280320)
[#2] 24.8°C  (heap=280120)
[#3] 25.1°C  (heap=280320)
```
Board A keeps publishing regardless of Board B's status.

### Board B — Serial Output (after reconnect)

```
╔══════════════════════════════════════╗
║  Project 2 — Exactly-Once Logger     ║
║  Board B · Disconnect & Replay Demo  ║
╚══════════════════════════════════════╝

>>> EXPERIMENT: Unplug me mid-stream, wait 20s,
>>> plug back in, and watch the gap-free replay!

[seq=1 total=1] {"n":1,"temp":25.3,"heap":280320}
...
─────────────────────────────────────────
  Stats: 10 received, 0 gaps, 0 duplicates
─────────────────────────────────────────
```

After reconnect, the stats should still show **0 gaps, 0 duplicates**.

## How to Monitor

### Prometheus (laptop)

```bash
# Watch publish count climb even while Board B is disconnected
curl -s http://localhost:9091/metrics | grep metricmq_messages_published_total

# Check ACK tracking efficiency
curl -s http://localhost:9091/metrics | grep metricmq_ack_tracking
```

### redis-cli (laptop)

```bash
redis-cli -p 6379 SUBSCRIBE sensors/temperature
```

## Why This Matters

| Broker | What happens when subscriber disconnects for 20s |
|--------|--------------------------------------------------|
| MQTT QoS 0 | Messages **lost forever** |
| MQTT QoS 1 | Messages replayed, but **possibly duplicated** |
| Redis Pub/Sub | Messages **lost forever** |
| **MetricMQ** | **Exactly the missed messages replayed, no duplicates** |

## Troubleshooting

| Problem | Fix |
|---------|-----|
| Board B shows `[GAP]` after reconnect | Make sure the same `client_id` ("logger-B") is used in both `connect()` calls |
| Board B shows `[DUPLICATE]` | This should never happen — file a bug |
| No replay after reconnect | Broker may have been restarted (LMDB state reset); restart the experiment |
| Board A stops publishing | Check WiFi; check broker is still running |
