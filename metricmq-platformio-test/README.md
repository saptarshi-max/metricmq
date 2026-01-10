# MetricMQ ESP8266 PlatformIO Test

This is a complete PlatformIO project demonstrating MetricMQ on ESP8266 with real hardware.

## Hardware Required

- ESP8266 board (NodeMCU, Wemos D1 Mini, etc.)
- Built-in LED (GPIO 2) - no external components needed!

## Wiring

```
ESP8266 Board
├── GPIO 2  → Built-in LED (connection indicator)
├── A0      → Analog input (temperature sensor simulation)
└── USB     → Serial monitor
```

**No external components required!** Uses the built-in LED and analog pin.

## Setup Instructions

1. **Install PlatformIO** (VS Code extension or CLI)

2. **Update WiFi credentials** in `src/main.cpp`:
   ```cpp
   const char* WIFI_SSID = "YOUR_WIFI_SSID";
   const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
   ```

3. **Update broker IP** in `src/main.cpp`:
   ```cpp
   const char* BROKER_HOST = "192.168.1.100";  // Your PC's IP
   ```

4. **Start MetricMQ broker** on your PC:
   ```bash
   cd build/Release
   ./metricmq-broker.exe
   ```

5. **Upload to ESP8266**:
   ```bash
   pio run -t upload
   ```

6. **Monitor serial output**:
   ```bash
   pio device monitor
   ```

## Expected Behavior

- Built-in LED stays ON when connected to broker
- LED blinks every 10 seconds when publishing sensor data
- Serial monitor shows uptime, free heap, WiFi signal strength
- Broker receives ESP8266 system data on `sensors/esp8266` topic

## Testing Commands

You can also test with the broker's built-in tools:

```bash
# Subscribe to sensor data
./sub_only.exe

# Publish commands to ESP8266
./pub_only.exe
```

## Troubleshooting ESP8266 Issues

### If your ESP8266 doesn't work:

1. **Check power supply**: ESP8266 needs stable 3.3V, not 5V
2. **Try different USB port/cable**: Some cables are power-only
3. **Check board selection**: Make sure PlatformIO detects your board correctly
4. **Update ESP8266 board package**: `pio pkg update`

### Common ESP8266 Problems:

- **Brownout resets**: Add capacitor across 3.3V/GND
- **WiFi connection issues**: Try different channels, check signal strength
- **Upload failures**: Hold FLASH button during upload, check COM port

## Taking Pictures/Videos for Proof

1. **Photo 1**: ESP8266 board with LED lit (showing connection)
2. **Screenshot 1**: PlatformIO upload success
3. **Screenshot 2**: Serial monitor with publishing messages
4. **Video**: LED blinking during data transmission
5. **Screenshot 3**: Broker logs showing ESP8266 data