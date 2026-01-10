#include <ESP8266WiFi.h>
#include <MetricMQ.h>

// WiFi credentials (change to your network)
const char* WIFI_SSID = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// MetricMQ broker configuration
const char* BROKER_HOST = "192.168.1.100";  // Change to your PC's IP
const uint16_t BROKER_PORT = 6379;

// Create MetricMQ client
MetricMQClient mqClient;

// Timing
unsigned long lastPublish = 0;
const unsigned long PUBLISH_INTERVAL = 10000;  // 10 seconds

// LED pins for ESP8266 (built-in LED is GPIO 2)
#define LED_PIN 2  // Built-in LED on ESP8266

void onMessage(const String& topic, const uint8_t* payload, size_t length, uint64_t sequence) {
  Serial.printf("📥 Received on %s (seq: %llu): ", topic.c_str(), sequence);
  for (size_t i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();
}

void setup() {
  Serial.begin(115200);
  delay(500);

  // Initialize LED
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);  // ESP8266 LED is active LOW

  // Welcome message
  Serial.println("\n╔═══════════════════════════════════════╗");
  Serial.println("║   MetricMQ ESP8266 PlatformIO Test   ║");
  Serial.println("║   Simple Publisher Demo               ║");
  Serial.println("╚═══════════════════════════════════════╝\n");

  // Generate client ID
  uint32_t chipid = ESP.getChipId();
  String clientId = "esp8266_" + String(chipid, HEX);
  Serial.printf("✓ Client ID: %s\n", clientId.c_str());

  // Connect to WiFi
  Serial.printf("→ Connecting to WiFi: %s\n", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✓ WiFi connected!");
    Serial.printf("  IP: %s\n", WiFi.localIP().toString().c_str());

    // Blink LED 3 times to show WiFi connected
    for (int i = 0; i < 3; i++) {
      digitalWrite(LED_PIN, LOW);  // LED ON
      delay(200);
      digitalWrite(LED_PIN, HIGH); // LED OFF
      delay(200);
    }
  } else {
    Serial.println("\n✗ WiFi connection failed!");
    Serial.println("⚠️  Update WIFI_SSID and WIFI_PASSWORD in the code");
  }

  // Initialize MetricMQ
  mqClient.begin(BROKER_HOST, BROKER_PORT);
  mqClient.setCallback(onMessage);

  Serial.println("\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
  Serial.println("Starting publishing...");
  Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
}

void loop() {
  // Check WiFi connection
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("✗ WiFi disconnected, reconnecting...");
    WiFi.reconnect();
    delay(5000);
    return;
  }

  // Maintain MetricMQ connection
  if (!mqClient.connected()) {
    Serial.printf("→ Connecting to MetricMQ broker at %s:%d\n", BROKER_HOST, BROKER_PORT);

    if (mqClient.connect()) {
      Serial.println("✓ Connected to MetricMQ broker!");
      digitalWrite(LED_PIN, LOW);  // LED ON to show connected

      // Subscribe to a topic
      mqClient.subscribe("esp8266/commands");
    } else {
      Serial.println("✗ Failed to connect to broker");
      digitalWrite(LED_PIN, HIGH); // LED OFF
      delay(5000);
    }
    return;
  }

  // Publish data periodically
  unsigned long now = millis();
  if (now - lastPublish >= PUBLISH_INTERVAL) {
    lastPublish = now;
    publishData();
  }

  // Process MetricMQ messages
  mqClient.loop();

  delay(100);
}

void publishData() {
  // Create JSON payload with ESP8266 info
  String payload = "{";
  payload += "\"uptime\":" + String(millis() / 1000) + ",";
  payload += "\"free_heap\":" + String(ESP.getFreeHeap()) + ",";
  payload += "\"chip_id\":\"" + String(ESP.getChipId(), HEX) + "\",";
  payload += "\"wifi_rssi\":" + String(WiFi.RSSI()) + ",";
  payload += "\"temperature\":" + String(analogRead(A0) * 3.3 / 1024.0 * 100, 1);  // Rough temp estimate
  payload += "}";

  // Print data
  Serial.println("\n┌─────────────────────────────────────┐");
  Serial.printf("│ ⏱️  Uptime:      %5lu sec       │\n", millis() / 1000);
  Serial.printf("│ 💾 Free Heap:   %5lu bytes     │\n", ESP.getFreeHeap());
  Serial.printf("│ 📶 WiFi RSSI:   %5d dBm       │\n", WiFi.RSSI());
  Serial.println("└─────────────────────────────────────┘");

  // Publish to MetricMQ
  String topic = "sensors/esp8266";
  if (mqClient.publish(topic, payload)) {
    Serial.printf("✓ Published to: %s\n", topic.c_str());

    // Blink LED to indicate publish
    digitalWrite(LED_PIN, HIGH); // LED OFF
    delay(100);
    digitalWrite(LED_PIN, LOW);  // LED ON
  } else {
    Serial.println("✗ Publish failed\n");
  }
}