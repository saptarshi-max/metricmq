# MetricMQ ESP8266 Wokwi Simulation with DHT22

Interactive ESP8266 simulation that publishes temperature and humidity data from DHT22 sensor to MetricMQ broker.

## What This Demonstrates

- ESP8266 running MetricMQ binary protocol client
- DHT22 temperature & humidity sensor readings
- Built-in LED connection indicator
- Host-to-simulation networking (broker on your PC, ESP8266 in Wokwi)

## Hardware Simulation

```
ESP8266 NodeMCU
├── GPIO 2  → DHT22 DATA (D4 pin)
├── GPIO 2  → Built-in LED (connection indicator)
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

1. Go to https://wokwi.com/projects/new/esp8266
2. Upload the files from `metricmq-wokwi-esp8266/`:
   - `diagram.json` → Circuit with ESP8266 + DHT22
   - `sketch.ino` → Main code with sensor reading
   - `libraries.txt` → Dependencies (DHTesp)
   - `wokwi.toml` → Configuration
3. Click **Start Simulation**
4. Watch the LED and serial monitor!
5. **Click on the DHT22 sensor** in Wokwi to adjust temperature/humidity!

## Expected Behavior

- Built-in LED stays ON when connected to broker
- LED blinks every 10 seconds when publishing sensor data
- Serial monitor shows temperature/humidity readings
- Broker receives ESP8266 sensor data on `sensors/esp8266/dht22` topic

## Interactive Sensor

**In Wokwi, you can:**
- Click the DHT22 sensor to open its properties
- Adjust temperature and humidity values
- Watch the ESP8266 respond to changing sensor readings
- See real-time updates in the serial monitor

## Taking Pictures/Videos for Proof

1. **Screenshot 1**: Wokwi editor with ESP8266 + DHT22 sensor
2. **Screenshot 2**: Serial monitor showing temperature/humidity readings
3. **Video**: LED blinking during publishing + sensor value changes
4. **Screenshot 3**: Broker terminal showing received sensor data

## Why ESP8266 + DHT22?

- **Real sensor simulation**: Shows actual IoT sensor integration
- **Memory constrained**: ESP8266 proves MetricMQ works on limited hardware
- **Interactive demo**: Change sensor values and see live updates
- **Complete IoT example**: From sensor → ESP8266 → broker

The simulation proves MetricMQ's reliability with real sensor data on resource-constrained ESP8266 hardware!