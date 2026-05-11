# Project 3 — Signed Secure Telemetry

**Feature demonstrated:** Ed25519 message signing, broker-side signature verification, topic authorization, subscriber-side local verification, and rejection of unsigned publishes.

## What It Does

- **Board A** (Signed Publisher) signs every message with its Ed25519 secret key and publishes to `secure/sensors/temperature`. At the 30-second mark, it deliberately sends an **unsigned** message to the same topic.
- **Board B** (Verifying Subscriber) subscribes to `secure/#` and verifies signatures locally using Board A's public key.
- **The broker** rejects the unsigned message — Board B never sees it.

## Hardware

- 2x ESP32-S3 N16R8 (16 MB Flash, 8 MB PSRAM)
- 2x USB-C data cables
- Laptop running the MetricMQ broker on the same WiFi network

## Setup (More Steps Than Other Projects)

This project requires generating cryptographic keys and registering them in the broker. Follow each step carefully.

### Step 0 — Generate Ed25519 Keys

On your laptop:

```powershell
cd MetricMQ\build\Release
.\metricmq-keygen.exe "sensor_node_1" "secure/sensors/*"
```

This prints:
1. A **64-byte secret key** array → paste into Board A's `main.cpp`
2. A **32-byte public key** array → paste into Board B's `main.cpp`
3. A **C++ broker registration snippet** → paste into the broker's `src/main.cpp`

**Save all three outputs.** You'll need them in the next steps.

### Step 1 — Register the Key in the Broker

Open `MetricMQ/src/main.cpp`. Find the line `metricmq::Broker broker(6379);` and add the key registration right after it:

```cpp
// Paste the exact bytes from keygen output:
uint8_t sensor_node_1_pk[32] = { /* 32 bytes from keygen */ };
broker.get_keystore().register_key(1, sensor_node_1_pk, {"secure/sensors/*"});
```

Then rebuild the broker:

```powershell
cmake --build build --config Release --target metricmq-broker
```

### Step 2 — Start the Broker

```powershell
.\build\Release\metricmq-broker.exe
```

### Step 3 — Configure Board A

Edit `board-a/src/main.cpp`:

1. Set your WiFi credentials and broker IP
2. **Paste the 64-byte secret key** into the `SECRET_KEY[64]` array (replace the placeholder zeros)

```cpp
const uint8_t SECRET_KEY[64] = {
    0xAB, 0xCD, ...  // ← your keygen output (64 bytes)
};
```

### Step 4 — Configure Board B

Edit `board-b/src/main.cpp`:

1. Set your WiFi credentials and broker IP
2. **Paste the 32-byte public key** into the `PUBLISHER_PUBLIC_KEY[32]` array

```cpp
const uint8_t PUBLISHER_PUBLIC_KEY[32] = {
    0x12, 0x34, ...  // ← the PUBLIC key from keygen (32 bytes)
};
```

### Step 5 — Flash Board A

```bash
cd demos/project3-signed-security/board-a
pio run --target upload
pio device monitor
```

### Step 6 — Flash Board B

```bash
cd demos/project3-signed-security/board-b
pio device list
pio run --target upload --upload-port COM4
pio device monitor --port COM4
```

## What to Expect

### Board A — Serial Output

```
╔══════════════════════════════════════════╗
║  Project 3 — Signed Secure Publisher      ║
║  Board A · Ed25519 · ESP32-S3 N16R8      ║
╚══════════════════════════════════════════╝

PSRAM: 8388608 bytes available
Connected! IP: 192.168.1.42
Connected. Ed25519 signing ENABLED. Key ID: 1

[SIGNED #1] secure/sensors/temperature -> {"temp":24.3,"msg":1,"heap":279000} (1247us)
[SIGNED #2] secure/sensors/temperature -> {"temp":25.1,"msg":2,"heap":279000} (1195us)
...

╔══════════════════════════════════════════════════╗
║  >>> SENDING UNSIGNED MESSAGE TO secure/ TOPIC   ║
║  >>> Broker should REJECT this.                  ║
║  >>> Board B should see NOTHING.                 ║
╚══════════════════════════════════════════════════╝

[UNSIGNED] Sent. Check broker console and Board B.

[SIGNED #7] secure/sensors/temperature -> {"temp":23.8,"msg":7,"heap":279000} (1210us)
```

### Board B — Serial Output

```
╔══════════════════════════════════════════╗
║  Project 3 — Verifying Subscriber         ║
║  Board B · Local Ed25519 Verification     ║
╚══════════════════════════════════════════╝

Registered publisher key_id=1 for local verification
Subscribed to secure/#

[VERIFIED seq=1 #1] secure/sensors/temperature -> {"temp":24.3,"msg":1,"heap":279000}
  Stats: 1 verified, 1 total
[VERIFIED seq=2 #2] secure/sensors/temperature -> {"temp":25.1,"msg":2,"heap":279000}
  Stats: 2 verified, 2 total
```

**At the 30-second mark:** Board A sends the unsigned message, but **Board B's serial shows nothing new**. The broker rejected it. The silence IS the proof.

### Broker Console — The Rejection

```
SIGNATURE_REQUIRED: unsigned publish to secure/ topic rejected
```

## The Key Moment

The unsigned message at 30 seconds is the money shot for your blog/demo. Three things to observe simultaneously:

| Terminal | What you see |
|----------|-------------|
| Board A serial | `>>> SENDING UNSIGNED MESSAGE TO secure/ TOPIC` |
| Broker console | `SIGNATURE_REQUIRED: unsigned publish to secure/ topic rejected` |
| Board B serial | **Nothing.** Complete silence. |

## Signing Performance

Watch the microsecond values Board A prints after each signed publish:

| Operation | Typical Time (ESP32-S3) |
|-----------|------------------------|
| Ed25519 Sign | ~1.2 ms (1200 us) |
| Ed25519 Verify | ~0.8 ms |
| Signing overhead per message | negligible at 5s intervals |

## How to Monitor

### Prometheus (laptop)

```bash
# Successful signature verifications
curl -s http://localhost:9091/metrics | grep metricmq_signature_verifications_total

# Rejected signatures / unsigned publishes
curl -s http://localhost:9091/metrics | grep metricmq_signature_failures_total
```

The `failures_total` counter should increment by 1 at the 30-second mark.

## Troubleshooting

| Problem | Fix |
|---------|-----|
| `SECRET_KEY is all zeros` warning | You forgot to paste the keygen output — re-run `metricmq-keygen` |
| `SIGNED_PUBLISH rejected` on ALL messages | The public key in the broker doesn't match the secret key on Board A; re-run keygen and re-register |
| Board B sees the unsigned message | The public key wasn't registered in the broker, or the broker wasn't rebuilt after adding it |
| Board B shows `[SIGNED ✗]` | Wrong public key on Board B — paste the correct one from keygen |
| No messages at all | Check both boards are on same WiFi, broker is running, firewall allows port 6379 |
