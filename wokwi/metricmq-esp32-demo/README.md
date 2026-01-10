# MetricMQ ESP32 Wokwi Simulation

Interactive ESP32 + DHT22 sensor simulation that publishes temperature and humidity data to MetricMQ broker.

## What This Demonstrates

- ESP32 running MetricMQ binary protocol client
- DHT22 temperature & humidity sensor readings
- LED indicators for connection and publish events
- Real-time serial monitoring
- Host-to-simulation networking (broker on your PC, ESP32 in Wokwi)

## Hardware Simulation

```
ESP32 DevKit V1
├── GPIO 4  → DHT22 sensor (temperature & humidity)
├── GPIO 2  → Green LED (publish indicator)
├── GPIO 15 → Blue LED (connection indicator)
└── Serial  → 115200 baud monitor
```

## Quick Start

### Prerequisites

1. **MetricMQ broker** running on your machine:
   ```bash
   cd build/Release
   ./metricmq-broker
   ```

2. **Wokwi account** (free): https://wokwi.com

### Run Simulation

#### Option 1: Wokwi Web IDE

1. Go to https://wokwi.com
2. Create new project → **ESP32**
3. Copy files from `wokwi/metricmq-esp32-demo/` to your project:
   - `diagram.json` → Circuit diagram
   - `wokwi.toml` → Configuration
   - `sketch.ino` → Main code
   - `libraries.txt` → Dependencies
4. Click **Start Simulation**
5. Watch the LEDs and serial monitor!

#### Option 2: VS Code with Wokwi Extension

1. Install **Wokwi for VS Code** extension
2. Open `wokwi/metricmq-esp32-demo/` folder
3. Press `F1` → "Wokwi: Start Simulator"
4. Monitor via Serial Monitor or Wokwi dashboard

## What You'll See

### Serial Monitor Output

```
╔═══════════════════════════════════════╗
║   MetricMQ ESP32 Wokwi Demo          ║
║   Temperature & Humidity Monitor      ║
╚═══════════════════════════════════════╝

✓ DHT22 sensor initialized
✓ Client ID: wokwi_esp32_a1b2c3d4
→ Connecting to WiFi: Wokwi-GUEST
.....
✓ WiFi connected!
  IP: 192.168.1.100
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
Starting sensor monitoring...
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

→ Connecting to MetricMQ broker at host.wokwi.internal:6379
✓ Connected to MetricMQ broker!

┌─────────────────────────────────────┐
│ 🌡️  Temperature:  24.0°C         │
│ 💧 Humidity:     55.0%          │
│ ⏱️  Uptime:         10 sec       │
└─────────────────────────────────────┘
✓ Published to: sensors/wokwi/dht22
  Payload: {"temperature":24.0,"humidity":55.0,"client_id":"wokwi_esp32_a1b2c3d4","uptime":10}
```

### LED Behavior

- **🔵 Blue LED (GPIO 15)** 
  - ON = Connected to broker
  - OFF = Disconnected
  - 3 quick blinks = WiFi connected

- **🟢 Green LED (GPIO 2)**
  - Blinks each time sensor data is published (every 10 seconds)

### Broker Console

```
[2026-01-07 14:30:15.102] [INFO] New client connected: fd=256
[2026-01-07 14:30:15.105] [DEBUG] Protocol detected: BINARY (fd=256)
[2026-01-07 14:30:20.045] [DEBUG] Published message [seq=1] to topic 'sensors/wokwi/dht22': 89 bytes -> 0 subscribers
[2026-01-07 14:30:30.055] [DEBUG] Published message [seq=2] to topic 'sensors/wokwi/dht22': 89 bytes -> 0 subscribers
```

## 🎮 Interactive Testing

### Adjust Sensor Values

1. In Wokwi simulator, click the **DHT22 sensor**
2. Slider controls appear:
   - **Temperature**: -40°C to 80°C
   - **Humidity**: 0% to 100%
3. Change values and watch the serial monitor update!

### Test Reconnection

1. Stop the broker (Ctrl+C)
2. Watch blue LED turn OFF
3. Serial shows reconnection attempts
4. Restart broker
5. Blue LED turns ON, green LED blinks resume

## 📡 Network Configuration

The `wokwi.toml` file forwards port 6379:

```toml
[[net.forward]]
port = 6379
host = "host.wokwi.internal"
```

This makes your local broker accessible to the simulated ESP32 at `host.wokwi.internal:6379`.

## Customization

### Change Publish Interval

In `sketch.ino`:
```cpp
const unsigned long PUBLISH_INTERVAL = 5000;  // 5 seconds instead of 10
```

### Change Topic

```cpp
String topic = "iot/mysensor/data";  // Custom topic
```

### Add More Sensors

Edit `diagram.json` to add components:
- LED (wokwi-led)
- Button (wokwi-pushbutton)
- Potentiometer (wokwi-potentiometer)
- OLED Display (wokwi-ssd1306)

## Files Explained

| File | Purpose |
|------|---------|
| `diagram.json` | Circuit schematic (ESP32, DHT22, LEDs, wiring) |
| `wokwi.toml` | Simulation config (network forwarding, firmware) |
| `sketch.ino` | Main Arduino code (sensor reading, MetricMQ client) |
| `libraries.txt` | Dependencies (DHTesp library) |

## 🐛 Troubleshooting

### "Connection failed" error

**Check:**
1. Is MetricMQ broker running? (`./metricmq-broker`)
2. Is broker listening on port 6379?
3. Is firewall blocking port 6379?

**Fix:**
```bash
# Allow port in Windows Firewall
netsh advfirewall firewall add rule name="MetricMQ" dir=in action=allow protocol=TCP localport=6379

# Or run broker on localhost
./metricmq-broker  # Should show "Broker listening on port 6379"
```

### Blue LED stays OFF

The broker is not reachable from Wokwi. Verify:
```bash
# On your PC, check broker is listening
netstat -an | findstr 6379

# Should show:
# TCP    0.0.0.0:6379           0.0.0.0:0              LISTENING
```

### DHT sensor shows NaN

Click the DHT22 in the simulator and ensure temperature/humidity are set (not blank).

### Green LED not blinking

Sensor data is publishing, but check:
1. Serial monitor shows "✓ Published to..."
2. Broker logs show received messages
3. LED might blink too fast (100ms) - watch carefully!

## 🎓 Learning Resources

### Binary Protocol Format

```
Publish Frame:
┌──────────┬─────────┬────────────┬───────┬──────────────┬─────────┐
│ Version  │ Command │ Topic Len  │ Topic │ Payload Len  │ Payload │
│  (1B)    │  (1B)   │   (2B)     │  (N)  │    (4B)      │   (M)   │
└──────────┴─────────┴────────────┴───────┴──────────────┴─────────┘
```

See `BINARY_PROTOCOL.md` for full specification.

## Next Steps

1. **Subscribe to data**: Create a subscriber to display real-time sensor data
2. **Grafana dashboard**: Visualize temperature/humidity over time
3. **Alerts**: Trigger alerts when temperature exceeds threshold
4. **Multiple sensors**: Simulate a sensor network with multiple ESP32s

## 📸 Screenshot

![Wokwi Simulation](https://wokwi.com/projects/screenshot.png)

(Circuit shows ESP32 with DHT22 sensor and two LEDs)

## 🔗 Links

- **Wokwi Platform**: https://wokwi.com
- **MetricMQ Docs**: ../README_NEW.md
- **Binary Protocol**: ../BINARY_PROTOCOL.md
- **ESP32 Library**: ../esp32-metricmq/

## License

MIT License - Same as MetricMQ project
