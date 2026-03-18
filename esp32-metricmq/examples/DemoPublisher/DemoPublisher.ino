/*
 * MetricMQ Demo: Publisher (ESP32-S3 N16R8 Board #1)
 * 
 * Demonstrates:
 *   - Binary protocol (40% smaller than RESP)
 *   - Exactly-once delivery with ACK
 *   - JSON sensor telemetry
 *   - Auto-reconnect resilience
 *   - Memory/performance monitoring (leverages 8MB PSRAM)
 * 
 * Hardware: ESP32-S3 N16R8 (16MB Flash, 8MB PSRAM)
 * Built-in RGB LED on GPIO 48 (most ESP32-S3 DevKitC boards)
 * 
 * Instructions:
 *   1. Set your WiFi credentials below
 *   2. Set BROKER_HOST to your PC's local IP (run ipconfig on PC)
 *   3. Start the broker on PC first: metricmq-broker.exe
 *   4. Upload this sketch to ESP32-S3 Board #1
 *   5. Open Serial Monitor at 115200 baud
 */

#include <WiFi.h>
#include <MetricMQ.h>

// ===== CONFIGURATION - UPDATE THESE =====
const char* WIFI_SSID     = "YourWiFiSSID";
const char* WIFI_PASSWORD  = "YourWiFiPassword";
const char* BROKER_HOST    = "192.168.1.100";  // Your PC's local IP
const uint16_t BROKER_PORT = 6379;
// =========================================

// RGB LED (ESP32-S3 DevKitC built-in)
#ifdef RGB_BUILTIN
  #define LED_PIN RGB_BUILTIN
#else
  #define LED_PIN 48  // Common for ESP32-S3
#endif

MetricMQClient mqClient;

// Publish intervals for different demo phases
unsigned long lastPublishFast = 0;
unsigned long lastPublishSlow = 0;
unsigned long lastStatsReport = 0;
unsigned long startTime = 0;

const unsigned long FAST_INTERVAL  = 1000;   // 1s - throughput demo
const unsigned long SLOW_INTERVAL  = 5000;   // 5s - normal telemetry
const unsigned long STATS_INTERVAL = 10000;  // 10s - memory/stats report

uint32_t publishCount = 0;
uint32_t failCount = 0;
bool burstMode = false;

// Demo phases
enum DemoPhase {
  PHASE_TELEMETRY,    // Normal sensor publishing (first 60s)
  PHASE_BURST,        // High-throughput burst (next 30s)
  PHASE_MULTI_TOPIC,  // Multi-topic fan-out (ongoing)
};

DemoPhase currentPhase = PHASE_TELEMETRY;

void setup() {
  Serial.begin(115200);
  delay(1000);

  // Initialize PSRAM if available
  if (psramFound()) {
    Serial.printf("PSRAM found: %d bytes\n", ESP.getPsramSize());
  }
  
  Serial.println("\n╔══════════════════════════════════════════╗");
  Serial.println("║  MetricMQ Demo - ESP32-S3 PUBLISHER      ║");
  Serial.println("║  Binary Protocol | Exactly-Once | N16R8   ║");
  Serial.println("╚══════════════════════════════════════════╝\n");

  // LED setup
  pinMode(LED_PIN, OUTPUT);

  // Connect WiFi
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.printf("Connecting to WiFi '%s'", WIFI_SSID);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 40) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\nFATAL: WiFi connection failed!");
    while (true) delay(1000);
  }

  Serial.println(" Connected!");
  Serial.printf("  IP: %s\n", WiFi.localIP().toString().c_str());
  Serial.printf("  RSSI: %d dBm\n", WiFi.RSSI());
  Serial.printf("  Channel: %d\n", WiFi.channel());

  // Configure MetricMQ
  mqClient.begin(BROKER_HOST, BROKER_PORT);
  mqClient.setClientId("esp32s3_pub_demo");
  mqClient.enableExactlyOnce(true);  // Enable ACK-based exactly-once
  mqClient.setKeepAlive(15);         // 15s keepalive

  // Connect to broker
  Serial.printf("\nConnecting to MetricMQ broker at %s:%d...\n", BROKER_HOST, BROKER_PORT);
  if (mqClient.connect()) {
    Serial.println(">>> CONNECTED to broker (binary protocol v1)");
    Serial.println(">>> Exactly-once delivery: ENABLED");
  } else {
    Serial.println("!!! Failed to connect - will retry in loop()");
  }

  startTime = millis();
  Serial.println("\n--- Phase 1: Sensor Telemetry (publishing every 5s) ---\n");
}

void loop() {
  mqClient.loop();

  // Reconnect logic
  if (!mqClient.isConnected()) {
    Serial.println("Connection lost, reconnecting...");
    if (mqClient.connect()) {
      Serial.println(">>> Reconnected!");
    } else {
      delay(3000);
      return;
    }
  }

  unsigned long now = millis();
  unsigned long elapsed = now - startTime;

  // Phase transitions
  if (elapsed > 60000 && elapsed < 90000 && currentPhase == PHASE_TELEMETRY) {
    currentPhase = PHASE_BURST;
    Serial.println("\n--- Phase 2: Throughput Burst (publishing every 1s) ---\n");
  } else if (elapsed >= 90000 && currentPhase == PHASE_BURST) {
    currentPhase = PHASE_MULTI_TOPIC;
    Serial.println("\n--- Phase 3: Multi-Topic Fan-out (3 topics every 5s) ---\n");
  }

  // Phase-specific publishing
  switch (currentPhase) {
    case PHASE_TELEMETRY:
      if (now - lastPublishSlow >= SLOW_INTERVAL) {
        lastPublishSlow = now;
        publishSensorData("sensors/esp32/telemetry");
      }
      break;

    case PHASE_BURST:
      if (now - lastPublishFast >= FAST_INTERVAL) {
        lastPublishFast = now;
        publishSensorData("sensors/esp32/burst");
      }
      break;

    case PHASE_MULTI_TOPIC:
      if (now - lastPublishSlow >= SLOW_INTERVAL) {
        lastPublishSlow = now;
        // Publish to 3 different topics simultaneously
        publishSensorData("sensors/esp32/temperature");
        publishSensorData("sensors/esp32/humidity");
        publishSystemStats("sensors/esp32/system");
      }
      break;
  }

  // Periodic stats report
  if (now - lastStatsReport >= STATS_INTERVAL) {
    lastStatsReport = now;
    printStats();
  }
}

void publishSensorData(const char* topic) {
  // Simulate sensor readings (realistic ranges)
  float temperature = 22.0 + random(-30, 30) / 10.0;
  float humidity    = 55.0 + random(-100, 100) / 10.0;
  float pressure    = 1013.25 + random(-50, 50) / 10.0;
  int rssi          = WiFi.RSSI();

  // JSON payload
  String payload = "{";
  payload += "\"temp\":" + String(temperature, 1);
  payload += ",\"hum\":" + String(humidity, 1);
  payload += ",\"pres\":" + String(pressure, 1);
  payload += ",\"rssi\":" + String(rssi);
  payload += ",\"seq\":" + String(publishCount + 1);
  payload += ",\"uptime\":" + String(millis() / 1000);
  payload += ",\"heap\":" + String(ESP.getFreeHeap());
  if (psramFound()) {
    payload += ",\"psram\":" + String(ESP.getFreePsram());
  }
  payload += "}";

  unsigned long t0 = micros();
  bool ok = mqClient.publish(topic, payload);
  unsigned long latency = micros() - t0;

  if (ok) {
    publishCount++;
    Serial.printf("[%6lus] PUB #%u -> %s (%u bytes, %luus)\n",
                  millis() / 1000, publishCount, topic, payload.length(), latency);
    blinkLED(50);  // Short green blink
  } else {
    failCount++;
    Serial.printf("[%6lus] FAIL #%u -> %s\n", millis() / 1000, failCount, topic);
  }
}

void publishSystemStats(const char* topic) {
  String payload = "{";
  payload += "\"free_heap\":" + String(ESP.getFreeHeap());
  payload += ",\"min_heap\":" + String(ESP.getMinFreeHeap());
  payload += ",\"heap_size\":" + String(ESP.getHeapSize());
  if (psramFound()) {
    payload += ",\"psram_free\":" + String(ESP.getFreePsram());
    payload += ",\"psram_size\":" + String(ESP.getPsramSize());
  }
  payload += ",\"cpu_freq\":" + String(ESP.getCpuFreqMHz());
  payload += ",\"wifi_rssi\":" + String(WiFi.RSSI());
  payload += ",\"uptime_s\":" + String(millis() / 1000);
  payload += ",\"pub_count\":" + String(publishCount);
  payload += ",\"fail_count\":" + String(failCount);
  payload += "}";

  if (mqClient.publish(topic, payload)) {
    publishCount++;
    Serial.printf("[%6lus] SYS -> %s\n", millis() / 1000, topic);
  }
}

void printStats() {
  Serial.println("\n┌─── Performance Stats ─────────────────┐");
  Serial.printf("│ Published:  %u messages\n", publishCount);
  Serial.printf("│ Failed:     %u messages\n", failCount);
  Serial.printf("│ Success:    %.1f%%\n", publishCount > 0 ? (100.0 * publishCount / (publishCount + failCount)) : 0.0);
  Serial.printf("│ Free Heap:  %u bytes\n", ESP.getFreeHeap());
  Serial.printf("│ Min Heap:   %u bytes\n", ESP.getMinFreeHeap());
  if (psramFound()) {
    Serial.printf("│ PSRAM Free: %u bytes\n", ESP.getFreePsram());
  }
  Serial.printf("│ WiFi RSSI:  %d dBm\n", WiFi.RSSI());
  Serial.printf("│ Uptime:     %lus\n", millis() / 1000);
  Serial.printf("│ Phase:      %s\n",
    currentPhase == PHASE_TELEMETRY ? "Telemetry" :
    currentPhase == PHASE_BURST ? "Burst" : "Multi-Topic");
  Serial.println("└───────────────────────────────────────┘\n");
}

void blinkLED(int ms) {
  digitalWrite(LED_PIN, HIGH);
  delay(ms);
  digitalWrite(LED_PIN, LOW);
}
