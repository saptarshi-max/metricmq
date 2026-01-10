# ESP32 Testing Guide for MetricMQ

**Goal**: Prove MetricMQ works on resource-constrained IoT devices (ESP32 with 520 KB RAM)

---

## 📋 Table of Contents

1. [Why ESP32 Testing Matters](#why-esp32-testing-matters)
2. [Hardware Specifications](#hardware-specifications)
3. [Option 1: Wokwi Simulator](#option-1-wokwi-simulator-easiest)
4. [Option 2: Real ESP32 Hardware](#option-2-real-esp32-hardware-recommended)
5. [Option 3: ESP-IDF Framework](#option-3-esp-idf-framework-advanced)
6. [Memory Budget Analysis](#memory-budget-analysis)
7. [Benchmarking on ESP32](#benchmarking-on-esp32)

---

## Why ESP32 Testing Matters

**MetricMQ's main selling point**: Lightweight enough to run on microcontrollers.

Without ESP32 testing, we cannot claim:
- ❌ "IoT-friendly"
- ❌ "Embedded-optimized"
- ❌ "Works on constrained devices"
- ❌ Any memory usage numbers

**With ESP32 testing**, we can prove:
- ✅ Actual RAM consumption
- ✅ Binary protocol efficiency
- ✅ Real-world IoT use cases
- ✅ Network performance on WiFi

---

## Hardware Specifications

### ESP32-WROOM-32 (Most Common)

```
MCU: Xtensa LX6 (240 MHz dual-core)
Flash: 4 MB
SRAM: 520 KB total
  ├─ DRAM (data): 320 KB
  ├─ IRAM (instructions): 200 KB
  └─ RTC memory: 16 KB
  
WiFi: 802.11 b/g/n (2.4 GHz)
Bluetooth: BLE 4.2
GPIO: 34 pins
Cost: ~$5-10 USD
```

### Memory Constraints

```
Total 520 KB SRAM
├─ WiFi/BT stack:  ~450 KB (reserved by ESP-IDF)
├─ FreeRTOS:       ~20 KB
└─ Available:      ~50-70 KB for your application ⚠️
```

**Challenge**: Fit MetricMQ client in <70 KB RAM

---

## Option 1: Wokwi Simulator (Easiest)

**Wokwi** (https://wokwi.com) is a free online ESP32 simulator.

### Advantages
- ✅ No hardware needed
- ✅ Instant testing in browser
- ✅ Easy to share (just URL)
- ✅ Built-in serial monitor

### Limitations
- ❌ Not real hardware (simulated RAM)
- ❌ Network latency differs
- ❌ Cannot measure actual power consumption

### Step-by-Step Wokwi Setup

#### 1. Create Broker on Your PC

```bash
# Terminal 1: Start MetricMQ broker
cd C:\Users\Sapta\Documents\Projects\MetricMQ\build\Release
.\metricmq-broker.exe

# Note your PC's IP address:
ipconfig  # Windows
# Look for "IPv4 Address" on your WiFi adapter
# Example: 192.168.1.100
```

#### 2. Create ESP32 Client Code

Save this as `esp32_metricmq_client.ino`:

```cpp
// ESP32 MetricMQ Client for Wokwi
#include <WiFi.h>

// Configuration
const char* WIFI_SSID = "Wokwi-GUEST";  // Wokwi's default WiFi
const char* WIFI_PASS = "";
const char* BROKER_IP = "192.168.1.100";  // ⚠️ CHANGE TO YOUR PC'S IP
const uint16_t BROKER_PORT = 6379;

WiFiClient tcpClient;

// Simple RESP protocol helper
void sendRESP(const String& command) {
  tcpClient.print(command);
  Serial.print("SENT: " + command);
}

// PUBLISH command in RESP format
String makePublish(const String& topic, const String& payload) {
  // *3\r\n$7\r\nPUBLISH\r\n$<topic_len>\r\n<topic>\r\n$<payload_len>\r\n<payload>\r\n
  String cmd = "*3\r\n$7\r\nPUBLISH\r\n$";
  cmd += String(topic.length());
  cmd += "\r\n";
  cmd += topic;
  cmd += "\r\n$";
  cmd += String(payload.length());
  cmd += "\r\n";
  cmd += payload;
  cmd += "\r\n";
  return cmd;
}

// SUBSCRIBE command in RESP format
String makeSubscribe(const String& topic) {
  String cmd = "*2\r\n$9\r\nSUBSCRIBE\r\n$";
  cmd += String(topic.length());
  cmd += "\r\n";
  cmd += topic;
  cmd += "\r\n";
  return cmd;
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n=== MetricMQ ESP32 Client ===");
  
  // Connect to WiFi
  Serial.print("Connecting to WiFi");
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  Serial.println("\nWiFi connected!");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  
  // Connect to MetricMQ broker
  Serial.print("Connecting to broker at ");
  Serial.print(BROKER_IP);
  Serial.print(":");
  Serial.println(BROKER_PORT);
  
  if (tcpClient.connect(BROKER_IP, BROKER_PORT)) {
    Serial.println("✅ Connected to MetricMQ broker!");
    
    // Subscribe to sensor/temp topic
    sendRESP(makeSubscribe("sensor/temp"));
    
  } else {
    Serial.println("❌ Connection failed!");
    Serial.println("Check broker IP and port");
  }
}

void loop() {
  // Publish temperature every 5 seconds
  static unsigned long lastPublish = 0;
  
  if (millis() - lastPublish > 5000) {
    if (tcpClient.connected()) {
      // Simulate temperature sensor
      float temp = random(200, 300) / 10.0;  // 20.0 - 30.0°C
      
      String payload = String(temp, 1) + "C";
      sendRESP(makePublish("sensor/temp", payload));
      
      Serial.println("📤 Published: " + payload);
      lastPublish = millis();
    }
  }
  
  // Receive messages
  while (tcpClient.available()) {
    String line = tcpClient.readStringUntil('\n');
    Serial.println("📥 Received: " + line);
  }
  
  // Check connection
  if (!tcpClient.connected()) {
    Serial.println("⚠️ Disconnected from broker");
    delay(5000);
    ESP.restart();  // Reconnect
  }
}
```

#### 3. Create Wokwi Project

1. Go to https://wokwi.com/projects/new/esp32
2. Replace the code with the above
3. Create `diagram.json`:

```json
{
  "version": 1,
  "author": "MetricMQ Team",
  "editor": "wokwi",
  "parts": [
    {
      "type": "wokwi-esp32-devkit-v1",
      "id": "esp",
      "top": 0,
      "left": 0,
      "attrs": {}
    }
  ],
  "connections": [],
  "dependencies": {}
}
```

4. Click "Start Simulation"

#### 4. Test Scenario

```
Expected Output on ESP32 Serial Monitor:
────────────────────────────────────────
=== MetricMQ ESP32 Client ===
Connecting to WiFi....
WiFi connected!
IP address: 192.168.4.2
Connecting to broker at 192.168.1.100:6379
✅ Connected to MetricMQ broker!
SENT: *2\r\n$9\r\nSUBSCRIBE\r\n$11\r\nsensor/temp\r\n
📤 Published: 25.3C
SENT: *3\r\n$7\r\nPUBLISH\r\n$11\r\nsensor/temp\r\n$6\r\n25.3C\r\n
📥 Received: +OK
📤 Published: 27.1C
```

**Expected Output on Broker**:
```
Broker listening on port 6379
New client connected: 192.168.4.2:51234
SUBSCRIBE: sensor/temp
PUBLISH: sensor/temp (6 bytes)
```

---

## Option 2: Real ESP32 Hardware (Recommended)

### Hardware Shopping List

| Item | Cost | Link |
|------|------|------|
| ESP32 DevKit | $8 | Amazon/AliExpress |
| USB-C Cable | $3 | Included usually |
| DHT22 Sensor | $4 | (Optional) |
| Breadboard | $3 | (Optional) |

**Total**: ~$10-15

### Setup Steps

#### 1. Install Arduino IDE
- Download: https://www.arduino.cc/en/software
- Install ESP32 board support:
  - File → Preferences
  - Add to "Additional Boards Manager URLs":
    ```
    https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
    ```
  - Tools → Board → Boards Manager
  - Search "ESP32" and install

#### 2. Upload Code

1. Connect ESP32 via USB
2. Select board: Tools → Board → ESP32 Dev Module
3. Select port: Tools → Port → COM3 (or similar)
4. Paste the code from Wokwi section above
5. **Update WIFI_SSID and WIFI_PASS to your network**
6. **Update BROKER_IP to your PC's IP address**
7. Click Upload

#### 3. Monitor Serial Output

- Tools → Serial Monitor (115200 baud)
- Watch for connection messages

### Real Hardware Benefits

✅ **Accurate RAM measurement**:
```cpp
void printMemoryStats() {
  Serial.print("Free heap: ");
  Serial.print(ESP.getFreeHeap());
  Serial.println(" bytes");
  
  Serial.print("Min free heap: ");
  Serial.print(ESP.getMinFreeHeap());
  Serial.println(" bytes");
}
```

✅ **Actual WiFi performance**:
- Real network latency
- Packet loss handling
- Power consumption

✅ **Integration with sensors**:
```cpp
#include <DHT.h>

DHT dht(4, DHT22);  // Pin 4, DHT22 sensor

void setup() {
  dht.begin();
}

void loop() {
  float temp = dht.readTemperature();
  sendRESP(makePublish("sensor/temp", String(temp)));
}
```

---

## Option 3: ESP-IDF Framework (Advanced)

**ESP-IDF** is Espressif's official development framework (more professional than Arduino).

### Advantages
- ✅ Better memory control
- ✅ FreeRTOS task management
- ✅ Precise heap tracking
- ✅ Production-ready

### Setup (Linux/Windows)

```bash
# Install ESP-IDF (Linux)
git clone --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
./install.sh esp32
. ./export.sh

# Create project
idf.py create-project metricmq_client
cd metricmq_client
```

### Example Code (main.c)

```c
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "lwip/sockets.h"

static const char *TAG = "MetricMQ";

#define BROKER_IP "192.168.1.100"
#define BROKER_PORT 6379

void metricmq_task(void *pvParameters) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    
    struct sockaddr_in broker_addr;
    broker_addr.sin_family = AF_INET;
    broker_addr.sin_port = htons(BROKER_PORT);
    inet_pton(AF_INET, BROKER_IP, &broker_addr.sin_addr);
    
    if (connect(sock, (struct sockaddr*)&broker_addr, sizeof(broker_addr)) == 0) {
        ESP_LOGI(TAG, "Connected to broker!");
        
        // PUBLISH command
        const char* publish_cmd = "*3\r\n$7\r\nPUBLISH\r\n$11\r\nsensor/temp\r\n$5\r\n25.5C\r\n";
        send(sock, publish_cmd, strlen(publish_cmd), 0);
        
        // Log memory
        ESP_LOGI(TAG, "Free heap: %d bytes", esp_get_free_heap_size());
        ESP_LOGI(TAG, "Min free heap: %d bytes", esp_get_minimum_free_heap_size());
    }
    
    close(sock);
    vTaskDelete(NULL);
}

void app_main(void) {
    nvs_flash_init();
    // ... WiFi initialization code ...
    
    xTaskCreate(metricmq_task, "metricmq", 4096, NULL, 5, NULL);
}
```

### Build and Flash

```bash
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

---

## Memory Budget Analysis

### ESP32 Available RAM: ~70 KB

**MetricMQ Client Memory Breakdown** (Estimated):

```
Component                        Size (bytes)
─────────────────────────────────────────────
WiFi client object               ~500
TCP buffer (send)                1,024
TCP buffer (receive)             1,024
RESP parser state                ~200
Message queue (10 msgs × 256B)   2,560
String overhead                  ~1,000
Stack (per task)                 4,096
─────────────────────────────────────────────
TOTAL ESTIMATED:                 ~10,404 bytes (~10 KB)

Remaining for application:       ~60 KB ✅
```

**Result**: MetricMQ client should fit comfortably!

### Binary Protocol Advantage

```
Example: Publish "sensor/temp" with "25.5C"

RESP Protocol:
  *3\r\n$7\r\nPUBLISH\r\n$11\r\nsensor/temp\r\n$5\r\n25.5C\r\n
  Total: 58 bytes

Binary Protocol:
  [Header: 16 bytes][Topic: 11 bytes][Payload: 5 bytes]
  Total: 32 bytes
  
Savings: 45% smaller ✅
```

For ESP32 with limited WiFi bandwidth, this matters!

---

## Benchmarking on ESP32

### What to Measure

#### 1. RAM Usage
```cpp
void printMemory() {
  Serial.println("=== Memory Stats ===");
  Serial.printf("Free heap: %d bytes\n", ESP.getFreeHeap());
  Serial.printf("Min free heap: %d bytes\n", ESP.getMinFreeHeap());
  Serial.printf("Heap fragmentation: %d%%\n", ESP.getHeapFragmentation());
}
```

#### 2. Throughput
```cpp
void benchmarkThroughput() {
  unsigned long start = millis();
  int msgCount = 1000;
  
  for (int i = 0; i < msgCount; i++) {
    sendRESP(makePublish("test", "data"));
  }
  
  unsigned long elapsed = millis() - start;
  float msgPerSec = (msgCount * 1000.0) / elapsed;
  
  Serial.printf("Sent %d messages in %lu ms\n", msgCount, elapsed);
  Serial.printf("Throughput: %.2f msg/sec\n", msgPerSec);
}
```

#### 3. Latency
```cpp
void benchmarkLatency() {
  for (int i = 0; i < 100; i++) {
    unsigned long start = micros();
    sendRESP(makePublish("test", "data"));
    // Wait for response
    while (!tcpClient.available()) {}
    tcpClient.read();
    unsigned long latency = micros() - start;
    
    Serial.printf("Round-trip: %lu µs\n", latency);
  }
}
```

#### 4. Power Consumption (Real Hardware Only)
```cpp
// Measure current draw with multimeter
// ESP32 typical: 80-160 mA (WiFi active)
//                10-20 mA (light sleep)
```

### Expected Results

**Realistic ESP32 Performance** (to be measured):

| Metric | Expected Range |
|--------|---------------|
| Free RAM | 40-60 KB |
| Throughput | 100-1000 msg/sec |
| Latency | 10-50 ms (WiFi dependent) |
| Power | 100-150 mA (WiFi active) |

**Compare to desktop broker**:
- Desktop: 1M+ msg/sec
- ESP32: 100-1000 msg/sec (1000x slower, but that's OK!)

---

## Test Scenarios

### Scenario 1: Simple Pub/Sub
```
[ESP32] --publish--> [Broker] --broadcast--> [Desktop Client]
         "sensor/temp: 25.5C"
```

### Scenario 2: Command & Control
```
[Desktop] --publish--> [Broker] --deliver--> [ESP32]
           "cmd/led: ON"          
                                 [ESP32 turns on LED]
```

### Scenario 3: Multi-Sensor Network
```
[ESP32 #1] ─┐
[ESP32 #2] ─┼─> [Broker] ─> [Dashboard]
[ESP32 #3] ─┘
  (3 temp sensors)
```

### Scenario 4: Edge Gateway
```
[ESP32 Sensors] -> [Raspberry Pi + MetricMQ] -> [Cloud]
   (10 devices)      (Local broker)              (Backup)
```

---

## Success Criteria

### Minimum Viable ESP32 Client

✅ **Must Have**:
- Connects to broker via WiFi
- Publishes sensor data every 5 seconds
- Uses <50 KB RAM
- Runs for 24+ hours without crash

✅ **Nice to Have**:
- Subscribes and receives commands
- Handles disconnect/reconnect
- Binary protocol support
- Deep sleep between publishes (power saving)

### Validation Checklist

- [ ] Code compiles for ESP32
- [ ] Connects to broker successfully
- [ ] Publishes RESP messages
- [ ] RAM usage measured (<50 KB)
- [ ] Runs for >1 hour continuously
- [ ] Handles WiFi reconnect
- [ ] Tested on Wokwi simulator
- [ ] Tested on real ESP32 hardware
- [ ] Documentation updated with results

---

## Next Steps

1. **Create `esp32/` folder in MetricMQ repo**
2. **Add Arduino example** (from this guide)
3. **Create Wokwi project link** (shareable URL)
4. **Test on real ESP32** (document results)
5. **Measure memory** (update estimates)
6. **Create YouTube tutorial** (10-minute demo)

---

## Resources

- **Wokwi**: https://wokwi.com/
- **ESP32 Docs**: https://docs.espressif.com/projects/esp-idf/
- **Arduino ESP32**: https://github.com/espressif/arduino-esp32
- **RESP Protocol**: https://redis.io/docs/reference/protocol-spec/

---

**Bottom Line**: ESP32 testing is CRITICAL to prove MetricMQ's IoT/embedded claims. Start with Wokwi for quick validation, then move to real hardware for accurate measurements.
