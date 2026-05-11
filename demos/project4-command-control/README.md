# Project 4 — Two-Way Command & Control

**Feature demonstrated:** Bidirectional pub/sub — both boards simultaneously publish AND subscribe. Wildcard topic matching, remote LED control, dynamic sampling interval, and cross-protocol interoperability (ESP32 binary + redis-cli RESP).

## What It Does

- **Board A** (Sensor Node) publishes telemetry every 5 seconds to `devices/board-a/telemetry` and listens for commands on `commands/board-a`.
- **Board B** (Control Panel) subscribes to Board A's telemetry and automatically sends a command every 15 seconds in a repeating cycle.
- Board A responds to commands by toggling its LED, changing its publishing interval, or sending a status report.

## Hardware

- 2x ESP32-S3 N16R8 (16 MB Flash, 8 MB PSRAM)
- 2x USB-C data cables
- Laptop running the MetricMQ broker on the same WiFi network

## Supported Commands

Board A responds to these commands on `commands/board-a`:

| Command | What Board A Does |
|---------|-------------------|
| `LED_ON` | Turns on the built-in LED (GPIO 2) |
| `LED_OFF` | Turns off the built-in LED |
| `INTERVAL:xxxx` | Changes publish interval (500-60000 ms) |
| `STATUS` | Publishes a status report to `devices/board-a/status` |

## Setup

### Step 1 — Start the Broker

```powershell
cd MetricMQ\build\Release
.\metricmq-broker.exe
```

### Step 2 — Configure Both Boards

Edit `board-a/src/main.cpp` and `board-b/src/main.cpp`:

```cpp
const char* WIFI_SSID     = "YourActualWiFi";
const char* WIFI_PASSWORD = "YourActualPassword";
const char* BROKER_HOST   = "192.168.29.168";  // Your laptop's IP
```

Find your laptop's IP:
```powershell
ipconfig | Select-String "IPv4"
```

### Step 3 — Flash Board A (Sensor Node)

```bash
cd demos/project4-command-control/board-a
pio run --target upload
pio device monitor
```

### Step 4 — Flash Board B (Control Panel)

In a second terminal:

```bash
cd demos/project4-command-control/board-b
pio device list          # Find Board B's COM port
pio run --target upload --upload-port COM4
pio device monitor --port COM4
```

### Step 5 — Watch the Show

Both boards are now running. Board B sends commands automatically every 15 seconds.

## What to Expect

### The Automated Command Cycle

Board B sends these commands in a loop:

| Time | Phase | Command | Board A Response |
|------|-------|---------|------------------|
| 0:15 | 1/5 | `LED_ON` | Built-in LED turns on |
| 0:30 | 2/5 | `INTERVAL:1000` | Telemetry speeds up from 5s to 1s |
| 0:45 | 3/5 | `STATUS` | Board A publishes a JSON status report |
| 1:00 | 4/5 | `INTERVAL:5000` | Telemetry slows back to 5s |
| 1:15 | 5/5 | `LED_OFF` | LED turns off |
| 1:30 | — | Cycle restarts | |

### Board A — Serial Output

```
================================================
  Project 4 - Sensor Node (Pub + Sub)
  Board A - ESP32-S3 N16R8
  Publishes telemetry, receives commands
================================================
PSRAM: 8388608 bytes available
Connected! IP: 192.168.1.42
Connected to MetricMQ broker
Listening for commands on: commands/board-a
Publishing telemetry every 5000ms to: devices/board-a/telemetry

[PUB #1] temp=24.3 led=OFF interval=5000 heap=279000
[PUB #2] temp=25.1 led=OFF interval=5000 heap=279000

[COMMAND seq=1] commands/board-a -> LED_ON
  -> LED turned ON

[PUB #3] temp=23.8 led=ON interval=5000 heap=279000

[COMMAND seq=2] commands/board-a -> INTERVAL:1000
  -> Publish interval changed to 1000ms

[PUB #4] temp=26.2 led=ON interval=1000 heap=278500
[PUB #5] temp=25.7 led=ON interval=1000 heap=278500
[PUB #6] temp=24.9 led=ON interval=1000 heap=278500
...
```

Notice the publishing rate visibly increasing after `INTERVAL:1000` — you'll see messages flooding in every second instead of every 5 seconds. This is the visual proof that the command was received and acted upon.

### Board B — Serial Output

```
================================================
  Project 4 - Control Panel (Sub + Pub)
  Board B - ESP32-S3 N16R8
  Watches telemetry, sends commands every 15s
================================================
PSRAM: 8388608 bytes available
Connected! IP: 192.168.1.43
Subscribed to Board A telemetry + status

[TELEMETRY seq=1 #1] {"temp":24.3,"led":"false","interval":5000,"msg":1,"heap":279000}
[TELEMETRY seq=2 #2] {"temp":25.1,"led":"false","interval":5000,"msg":2,"heap":279000}

>> [Phase 1/5] Sending: LED_ON

[TELEMETRY seq=3 #3] {"temp":23.8,"led":"true","interval":5000,"msg":3,"heap":279000}

>> [Phase 2/5] Sending: INTERVAL:1000

[TELEMETRY seq=4 #4] {"temp":26.2,"led":"true","interval":1000,"msg":4,"heap":278500}
[TELEMETRY seq=5 #5] {"temp":25.7,"led":"true","interval":1000,"msg":5,"heap":278500}
```

After `INTERVAL:1000`, you'll see telemetry arriving much faster — proof the command took effect.

### The Status Report

When Phase 3 fires, Board B receives the status report in a formatted box:

```
  ┌─────────────────────────────────────────┐
  │ STATUS REPORT from Board A               │
  ├─────────────────────────────────────────┤
  │ {"led":true,"interval":1000,"heap":278500,"rssi":-42,"uptime":55,"msgs_sent":12}
  └─────────────────────────────────────────┘
```

## Bonus: redis-cli Commands

While the demo is running, open a third terminal on your laptop and send commands manually:

```bash
# Turn the LED on from your laptop
redis-cli -p 6379 PUBLISH commands/board-a "LED_ON"

# Speed up to 500ms publishing
redis-cli -p 6379 PUBLISH commands/board-a "INTERVAL:500"

# Request a status report
redis-cli -p 6379 PUBLISH commands/board-a "STATUS"

# Slow back down
redis-cli -p 6379 PUBLISH commands/board-a "INTERVAL:5000"

# Turn LED off
redis-cli -p 6379 PUBLISH commands/board-a "LED_OFF"
```

This is the interoperability demo: three different protocols (ESP32 binary from Board A, ESP32 binary from Board B, and RESP from redis-cli) all talking through the same MetricMQ broker.

## What This Proves

| Feature | Proof |
|---------|-------|
| Bidirectional pub/sub | Both boards publish AND subscribe simultaneously |
| Remote control | LED responds to commands from Board B or redis-cli |
| Dynamic reconfiguration | Publish interval changes on-the-fly without reflash |
| Status queries | Board A responds to STATUS with heap, RSSI, uptime |
| Cross-protocol interop | redis-cli (RESP) and ESP32 (binary) on same topics |
| Low latency | Commands take effect within milliseconds |

## LED Pin Note

The code uses `GPIO 2` as the LED pin. On most ESP32-S3 DevKitC boards, this is the built-in LED. If your board has a different pinout:

```cpp
#define LED_PIN 2  // Change this to match your board
```

Common alternatives: `38` (some S3 DevKitC-1 variants), `48` (RGB LED data pin).

## Troubleshooting

| Problem | Fix |
|---------|-----|
| No telemetry on Board B | Check WiFi and broker IP on both boards |
| Commands not received | Verify Board A's subscribe topic matches Board B's publish topic (`commands/board-a`) |
| LED doesn't light | Try different GPIO pin — check your board's schematic |
| redis-cli doesn't work | Make sure broker is on port 6379 and laptop firewall allows it |
| Interval won't change | Value must be between 500 and 60000 ms |
