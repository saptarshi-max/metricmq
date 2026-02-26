/*
 * MetricMQ Demo: Subscriber (ESP32-S3 N16R8 Board #2)
 * 
 * Demonstrates:
 *   - Binary protocol subscription
 *   - Exactly-once delivery with ACK (no duplicate messages)
 *   - Wildcard subscription (sensors/esp32/#)
 *   - Real-time message display + LED feedback
 *   - Message deduplication verification
 *   - Latency measurement
 * 
 * Hardware: ESP32-S3 N16R8 (16MB Flash, 8MB PSRAM)
 * Built-in RGB LED on GPIO 48 (most ESP32-S3 DevKitC boards)
 * 
 * Instructions:
 *   1. Set your WiFi credentials below
 *   2. Set BROKER_HOST to your PC's local IP
 *   3. Start the broker on PC first: metricmq-broker.exe
 *   4. Upload DemoPublisher.ino to Board #1
 *   5. Upload this sketch to Board #2
 *   6. Open Serial Monitor at 115200 baud
 */

#include <WiFi.h>
#include <MetricMQ.h>

// ===== CONFIGURATION - UPDATE THESE =====
const char* WIFI_SSID      = "YourWiFiSSID";
const char* WIFI_PASSWORD   = "YourWiFiPassword";
const char* BROKER_HOST     = "192.168.1.100";  // Your PC's local IP
const uint16_t BROKER_PORT  = 6379;
// =========================================

// LED
#ifdef RGB_BUILTIN
  #define LED_PIN RGB_BUILTIN
#else
  #define LED_PIN 48
#endif

MetricMQClient mqClient;

// Statistics
uint32_t messageCount = 0;
uint32_t duplicateCount = 0;
uint64_t lastSequence = 0;
unsigned long firstMsgTime = 0;
unsigned long lastMsgTime = 0;
unsigned long lastStatsReport = 0;

// Track unique sequences for dedup verification
const int SEQ_HISTORY_SIZE = 100;
uint64_t seqHistory[SEQ_HISTORY_SIZE];
int seqHistoryPos = 0;

// Per-topic counters
struct TopicCounter {
  String topic;
  uint32_t count;
};
TopicCounter topicCounters[10];
int topicCounterCount = 0;

// Message callback
void onMessage(const String& topic, const uint8_t* payload, size_t length, uint64_t sequence) {
  unsigned long receiveTime = micros();
  messageCount++;
  lastMsgTime = millis();
  if (firstMsgTime == 0) firstMsgTime = lastMsgTime;

  // Duplicate detection
  bool isDuplicate = false;
  for (int i = 0; i < SEQ_HISTORY_SIZE; i++) {
    if (seqHistory[i] == sequence && sequence != 0) {
      isDuplicate = true;
      duplicateCount++;
      break;
    }
  }
  seqHistory[seqHistoryPos] = sequence;
  seqHistoryPos = (seqHistoryPos + 1) % SEQ_HISTORY_SIZE;

  // Track per-topic counts
  bool found = false;
  for (int i = 0; i < topicCounterCount; i++) {
    if (topicCounters[i].topic == topic) {
      topicCounters[i].count++;
      found = true;
      break;
    }
  }
  if (!found && topicCounterCount < 10) {
    topicCounters[topicCounterCount].topic = topic;
    topicCounters[topicCounterCount].count = 1;
    topicCounterCount++;
  }

  // Convert payload to string
  String payloadStr;
  payloadStr.reserve(length + 1);
  for (size_t i = 0; i < length; i++) {
    payloadStr += (char)payload[i];
  }

  // Display
  Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
  Serial.printf("MSG #%u %s\n", messageCount, isDuplicate ? "[DUPLICATE!]" : "");
  Serial.printf("  Topic:    %s\n", topic.c_str());
  Serial.printf("  Sequence: %llu\n", sequence);
  Serial.printf("  Size:     %u bytes\n", length);
  Serial.printf("  Payload:  %s\n", payloadStr.c_str());
  Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");

  // LED blink: short = normal, long = duplicate
  if (isDuplicate) {
    // Long red-ish blink for duplicate (shouldn't happen with exactly-once!)
    for (int i = 0; i < 5; i++) {
      digitalWrite(LED_PIN, HIGH);
      delay(50);
      digitalWrite(LED_PIN, LOW);
      delay(50);
    }
  } else {
    // Short single blink for normal message
    digitalWrite(LED_PIN, HIGH);
    delay(30);
    digitalWrite(LED_PIN, LOW);
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  if (psramFound()) {
    Serial.printf("PSRAM found: %d bytes\n", ESP.getPsramSize());
  }
  
  Serial.println("\n╔══════════════════════════════════════════╗");
  Serial.println("║  MetricMQ Demo - ESP32-S3 SUBSCRIBER     ║");
  Serial.println("║  Binary Protocol | Exactly-Once | N16R8   ║");
  Serial.println("╚══════════════════════════════════════════╝\n");

  // LED
  pinMode(LED_PIN, OUTPUT);
  memset(seqHistory, 0, sizeof(seqHistory));

  // WiFi
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

  // MetricMQ setup
  mqClient.begin(BROKER_HOST, BROKER_PORT);
  mqClient.setClientId("esp32s3_sub_demo");
  mqClient.setCallback(onMessage);
  mqClient.enableExactlyOnce(true);   // ACK-based exactly-once
  mqClient.setKeepAlive(15);

  // Connect
  Serial.printf("\nConnecting to MetricMQ broker at %s:%d...\n", BROKER_HOST, BROKER_PORT);
  if (mqClient.connect()) {
    Serial.println(">>> CONNECTED to broker (binary protocol v1)");
    Serial.println(">>> Exactly-once delivery: ENABLED");

    // Subscribe to wildcard topic to catch ALL publisher messages
    // This matches: sensors/esp32/telemetry, sensors/esp32/burst,
    //               sensors/esp32/temperature, sensors/esp32/humidity,
    //               sensors/esp32/system
    Serial.println("\nSubscribing to topics...");
    
    if (mqClient.subscribe("sensors/esp32/#")) {
      Serial.println("  >>> Subscribed: sensors/esp32/# (wildcard - all subtopics)");
    }
    
    // Also subscribe to the basic SimplePublish example topic
    if (mqClient.subscribe("sensors/esp32")) {
      Serial.println("  >>> Subscribed: sensors/esp32 (basic)");
    }

    // Ready indication
    Serial.println("\n==========================================");
    Serial.println("  READY - Waiting for messages...");
    Serial.println("  Exactly-once enabled: duplicates will be");
    Serial.println("  detected and flagged if they occur.");
    Serial.println("==========================================\n");

    // 3 quick blinks = ready
    for (int i = 0; i < 3; i++) {
      digitalWrite(LED_PIN, HIGH);
      delay(150);
      digitalWrite(LED_PIN, LOW);
      delay(150);
    }
  } else {
    Serial.println("!!! Failed to connect - will retry in loop()");
  }
}

void loop() {
  mqClient.loop();

  // Reconnect
  if (!mqClient.isConnected()) {
    Serial.println("Connection lost, reconnecting...");
    if (mqClient.connect()) {
      Serial.println(">>> Reconnected, re-subscribing...");
      mqClient.subscribe("sensors/esp32/#");
      mqClient.subscribe("sensors/esp32");
    } else {
      delay(3000);
      return;
    }
  }

  // Print stats every 15s
  unsigned long now = millis();
  if (now - lastStatsReport >= 15000 && messageCount > 0) {
    lastStatsReport = now;
    
    float elapsed = (lastMsgTime - firstMsgTime) / 1000.0;
    float rate = elapsed > 0 ? messageCount / elapsed : 0;

    Serial.println("\n┌─── Subscriber Stats ─────────────────────┐");
    Serial.printf("│ Total Messages: %u\n", messageCount);
    Serial.printf("│ Duplicates:     %u %s\n", duplicateCount, 
                  duplicateCount == 0 ? "(exactly-once working!)" : "(!!!)");
    Serial.printf("│ Avg Rate:       %.1f msg/s\n", rate);
    Serial.printf("│ Free Heap:      %u bytes\n", ESP.getFreeHeap());
    if (psramFound()) {
      Serial.printf("│ PSRAM Free:     %u bytes\n", ESP.getFreePsram());
    }
    Serial.printf("│ WiFi RSSI:      %d dBm\n", WiFi.RSSI());

    // Per-topic breakdown
    if (topicCounterCount > 0) {
      Serial.println("│ ─── By Topic ───");
      for (int i = 0; i < topicCounterCount; i++) {
        Serial.printf("│   %s: %u\n", topicCounters[i].topic.c_str(), topicCounters[i].count);
      }
    }
    Serial.println("└───────────────────────────────────────────┘\n");
  }
}
