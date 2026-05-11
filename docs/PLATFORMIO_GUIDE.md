# MetricMQ PlatformIO Step-by-Step Guide

This guide will walk you through setting up a PlatformIO project to use the **MetricMQ** library on ESP32 or ESP8266 hardware.

---

## 1. Prerequisites

Before starting, ensure you have:
- **VS Code** with the **PlatformIO IDE** extension installed, or the PlatformIO CLI.
- An **ESP32** or **ESP8266** development board.
- Your PC connected to the same WiFi network as your ESP board.
- The **MetricMQ Broker** built and running on your PC (or a server).

---

## 2. Create a New PlatformIO Project

1. Open **VS Code** and go to the **PlatformIO Home** screen.
2. Click **New Project**.
3. Fill in the details:
   - **Name**: `MetricMQ_Node` (or your preferred name)
   - **Board**: Select your specific board (e.g., `Espressif ESP32 Dev Module` or `Espressif ESP8266 NodeMCU 1.0`)
   - **Framework**: `Arduino`
4. Click **Finish**. PlatformIO will initialize the project folder.

---

## 3. Configure `platformio.ini`

Open the `platformio.ini` file in your new project folder and add the MetricMQ library to your dependencies.

> [!NOTE]
> Currently, the recommended approach is to use a dedicated MetricMQ client repository. 
> Ensure your boards and broker versions are compatible (see Compatibility Matrix below).

### Compatibility Matrix
| Broker Version | ESP Client Version | ESP32 Core | ESP8266 Core |
|----------------|--------------------|------------|--------------|
| v1.0.0+        | v1.0.0+            | 2.0.0+     | 3.0.0+       |

### For ESP32:
```ini
[env:esp32dev]
platform = espressif32
board = esp32dev
framework = arduino
monitor_speed = 115200
upload_speed = 921600

lib_deps =
    # MetricMQ library (includes ESP32 Arduino client)
    # Modify this url to point to your specific repo
    https://github.com/Saptarshi-max/MetricMQ.git
```

### For ESP8266:
```ini
[env:nodemcuv2]
platform = espressif8266
board = nodemcuv2
framework = arduino
monitor_speed = 115200
upload_speed = 921600

lib_deps =
    https://github.com/Saptarshi-max/MetricMQ.git
```

---

## 4. Write Your Application Code

Open `src/main.cpp` and replace its contents with the following basic example. This example connects to your WiFi, connects to the MetricMQ broker, and publishes a simple message every few seconds.

```cpp
#include <Arduino.h>
#include <WiFi.h>          // Use <ESP8266WiFi.h> if you are on an ESP8266
#include <MetricMQ.h>

// --- Configuration ---
// WARNING: Do not commit your WiFi credentials or any secret keys to public version control!
// Consider using a separate secrets header file for production deployments.
const char* WIFI_SSID     = "Your_WiFi_SSID";
const char* WIFI_PASSWORD = "Your_WiFi_Password";

// IP address of the PC running the MetricMQ Broker
const char* BROKER_HOST   = "192.168.1.100";  // CHANGE THIS to your PC's IP Address
const uint16_t BROKER_PORT = 6379;

// --- Globals ---
MetricMQClient mqClient;
unsigned long lastPublish = 0;

void setup() {
    Serial.begin(115200);
    delay(10);

    Serial.println("\n--- MetricMQ PlatformIO Node ---");
    Serial.print("Connecting to WiFi: ");
    Serial.println(WIFI_SSID);

    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nWiFi connected.");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());

    // Startup Self-Check for Default Broker IP
    if (String(BROKER_HOST) == "192.168.1.100") {
        Serial.println("\n[WARNING] BROKER_HOST is still using the default '192.168.1.100'.");
        Serial.println("[WARNING] Please update it in the code to your actual Broker IP address!\n");
    }

    // Initialize MetricMQ Client
    mqClient.begin(BROKER_HOST, BROKER_PORT);

    // Initial publish test on boot to verify connectivity
    mqClient.publish("sensors/status", "{\"status\":\"booted\"}");
}

void loop() {
    // Keep connection alive and process incoming data
    mqClient.loop();

    // Check if we are connected; print status
    // NOTE: The MetricMQClient does NOT auto-reconnect by default. 
    // It is exclusively your application's responsibility to handle reconnects!
    if (!mqClient.connected()) {
        Serial.println("MetricMQ disconnected. Attempting to reconnect...");
        mqClient.reconnect();
        delay(2000); // Retry delay
        return;
    }

    // Publish telemetry every 5 seconds
    if (millis() - lastPublish > 5000) {
        lastPublish = millis();
        
        String payload = "{\"uptime_ms\":" + String(millis()) + "}";
        Serial.print("Publishing to sensors/telemetry: ");
        Serial.println(payload);

        bool success = mqClient.publish("sensors/telemetry", payload.c_str());
        if (!success) {
            Serial.println("Publish failed.");
        }
    }
}
```

**Make sure you update:**
- `WIFI_SSID` and `WIFI_PASSWORD`
- `BROKER_HOST` with your PC's local IP address (find it using `ipconfig` on Windows or `ifconfig` on macOS/Linux).

---

## 5. Build and Upload

1. Save `main.cpp` and `platformio.ini`.
2. Connect your ESP board to your PC via USB.
3. Click the **PlatformIO: Build** checkmark (✓) at the bottom of VS Code to compile the code. PlatformIO will automatically download the MetricMQ dependency from GitHub.
4. Click the **PlatformIO: Upload** arrow (→) to flash the code to your board.
5. Once uploaded, click the **PlatformIO: Serial Monitor** plug icon to view the output.

---

## 6. Run the Broker & Verify

1. Open a terminal on your PC.
2. Start the MetricMQ broker from your build directory (e.g., `build\Release\metricmq-broker.exe`).
3. Check the ESP Serial Monitor. You should see it connect to the WiFi and then successfully connect to the broker.
4. Check the broker's terminal output. You should see a new connection from your ESP board's IP address.

### End-to-End Smoke Test

Follow these exact steps to verify the entire system works correctly:

1. **Start the Broker:** Open a terminal on your computer, navigate to the build output folder (e.g. `cd build\Release`), and run `.\metricmq-broker.exe`. Ensure it says "Listening on 0.0.0.0:6379".
2. **Flash the ESP:** Click *PlatformIO: Upload*, then immediately open the *Serial Monitor* (`pio device monitor -b 115200`). Wait for the "WiFi connected" and the boot status warnings.
3. **Verify Publisher Output:** The ESP's serial monitor should repeatedly display `Publishing to sensors/telemetry: ...`.
4. **Subscribe to Data:** Open a *second* terminal window on your PC to act as the receiver:
   ```bash
   cd build\Release
   .\sub_only.exe
   
   # Alternatively, if you use redis-cli:
   # redis-cli -h 127.0.0.1 -p 6379 subscribe "sensors/telemetry"
   ```
5. **Confirm Traffic:** You should see matching JSON telemetry logs appearing in the receiver's terminal window every 5 seconds!

---

## Troubleshooting (First-Run Failures)

If things aren't working, check the table below:

| Symptom | Probable Cause | Fix / Verification |
|---------|----------------|--------------------|
| **Connection Refused** | Broker not started | Check if `metricmq-broker.exe` is actually running and listening on port 6379. |
| **Timeout / No Route** | Wrong `BROKER_HOST` | Open a command prompt, run `ipconfig` (Windows) or `ifconfig` (Linux/macOS) to find your PC's real IPv4 address, and update `BROKER_HOST`. |
| **Timeout / Blocked** | Firewall blocking | Add an inbound Windows Firewall rule allowing TCP port 6379 for `metricmq-broker.exe`. |
| **Unreachable** | Different subnet | Ensure both the PC and ESP are on the exact same Wi-Fi network (e.g., both on the 2.4GHz band of the same router). |
| **Garbage Serial Output**| Baud rate mismatch | Ensure `monitor_speed = 115200` in `platformio.ini` and `Serial.begin(115200);` in code match perfectly. |
| **Build Fails** | Library path wrong | Check that your `lib_deps` URL is correct and public, or point directly to a valid local folder URL. |
