/*
 * MetricMQ ESP32 Wokwi Demo
 * 
 * This is a complete demonstration of MetricMQ on ESP32 with:
 * - DHT22 temperature & humidity sensor
 * - Green LED: Blinks when publishing data
 * - Blue LED: Blinks when connected to broker
 * - Serial output: Shows real-time status
 * 
 * Hardware (simulated in Wokwi):
 * - ESP32 DevKit V1
 * - DHT22 sensor on GPIO 4
 * - Green LED on GPIO 2 (publish indicator)
 * - Blue LED on GPIO 15 (connection indicator)
 * 
 * Setup Instructions:
 * 1. Start MetricMQ broker on your machine: ./metricmq-broker
 * 2. Click "Start Simulation" in Wokwi
 * 3. Watch the LEDs and serial monitor
 * 4. Adjust DHT22 temperature/humidity in simulator
 * 
 * Expected Behavior:
 * - Blue LED stays ON when connected to broker
 * - Green LED blinks every time sensor data is published (10 sec)
 * - Serial monitor shows connection status and sensor readings
 * - Broker receives temperature/humidity updates
 */

#include <WiFi.h>
#include "DHTesp.h"

// Pin definitions
#define DHT_PIN 4
#define LED_PUBLISH 2    // Green LED - blinks on publish
#define LED_CONNECT 15   // Blue LED - shows connection status

// WiFi credentials (Wokwi provides simulated WiFi)
const char* WIFI_SSID = "Wokwi-GUEST";
const char* WIFI_PASSWORD = "";

// MetricMQ broker configuration
// In Wokwi, use host.wokwi.internal to reach host machine
const char* BROKER_HOST = "host.wokwi.internal";
const uint16_t BROKER_PORT = 6379;

// Create DHT sensor instance
DHTesp dht;

// WiFi client
WiFiClient client;

// Connection state
bool connected = false;
String clientId;

// Timing
unsigned long lastPublish = 0;
unsigned long lastReconnect = 0;
const unsigned long PUBLISH_INTERVAL = 10000;  // 10 seconds
const unsigned long RECONNECT_INTERVAL = 5000;  // 5 seconds

// Binary protocol constants
#define BINARY_PROTOCOL_VERSION 0x01
#define CMD_PUBLISH 0x03

void setup() {
  Serial.begin(115200);
  delay(500);
  
  // Initialize LEDs
  pinMode(LED_PUBLISH, OUTPUT);
  pinMode(LED_CONNECT, OUTPUT);
  digitalWrite(LED_PUBLISH, LOW);
  digitalWrite(LED_CONNECT, LOW);
  
  // Welcome message
  Serial.println("\n╔═══════════════════════════════════════╗");
  Serial.println("║   MetricMQ ESP32 Wokwi Demo          ║");
  Serial.println("║   Temperature & Humidity Monitor      ║");
  Serial.println("╚═══════════════════════════════════════╝\n");
  
  // Initialize DHT sensor
  dht.setup(DHT_PIN, DHTesp::DHT22);
  Serial.println("✓ DHT22 sensor initialized");
  
  // Generate client ID from MAC
  uint64_t chipid = ESP.getEfuseMac();
  clientId = "wokwi_esp32_" + String((uint32_t)chipid, HEX);
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
    
    // Blink blue LED 3 times to show WiFi connected
    for (int i = 0; i < 3; i++) {
      digitalWrite(LED_CONNECT, HIGH);
      delay(200);
      digitalWrite(LED_CONNECT, LOW);
      delay(200);
    }
  } else {
    Serial.println("\n✗ WiFi connection failed!");
  }
  
  Serial.println("\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
  Serial.println("Starting sensor monitoring...");
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
  
  // Maintain broker connection
  if (!client.connected()) {
    connected = false;
    digitalWrite(LED_CONNECT, LOW);
    
    unsigned long now = millis();
    if (now - lastReconnect >= RECONNECT_INTERVAL) {
      lastReconnect = now;
      connectToBroker();
    }
    return;
  }
  
  // Update connection LED
  if (connected) {
    digitalWrite(LED_CONNECT, HIGH);
  }
  
  // Publish sensor data periodically
  unsigned long now = millis();
  if (now - lastPublish >= PUBLISH_INTERVAL) {
    lastPublish = now;
    publishSensorData();
  }
  
  delay(100);  // Small delay to prevent CPU overload
}

bool connectToBroker() {
  Serial.printf("→ Connecting to MetricMQ broker at %s:%d\n", BROKER_HOST, BROKER_PORT);
  
  if (client.connect(BROKER_HOST, BROKER_PORT)) {
    connected = true;
    Serial.println("✓ Connected to MetricMQ broker!");
    digitalWrite(LED_CONNECT, HIGH);
    return true;
  } else {
    Serial.println("✗ Failed to connect to broker");
    return false;
  }
}

void publishSensorData() {
  // Read sensor
  TempAndHumidity data = dht.getTempAndHumidity();
  
  if (dht.getStatus() != 0) {
    Serial.println("✗ DHT sensor read error!");
    return;
  }
  
  // Create JSON payload
  String payload = "{";
  payload += "\"temperature\":" + String(data.temperature, 1) + ",";
  payload += "\"humidity\":" + String(data.humidity, 1) + ",";
  payload += "\"client_id\":\"" + clientId + "\",";
  payload += "\"uptime\":" + String(millis() / 1000);
  payload += "}";
  
  // Print sensor readings
  Serial.println("\n┌─────────────────────────────────────┐");
  Serial.printf("│ 🌡️  Temperature: %5.1f°C         │\n", data.temperature);
  Serial.printf("│ 💧 Humidity:    %5.1f%%          │\n", data.humidity);
  Serial.printf("│ ⏱️  Uptime:      %5lu sec       │\n", millis() / 1000);
  Serial.println("└─────────────────────────────────────┘");
  
  // Publish to MetricMQ
  String topic = "sensors/wokwi/dht22";
  if (sendBinaryPublish(topic, payload)) {
    Serial.printf("✓ Published to: %s\n", topic.c_str());
    Serial.printf("  Payload: %s\n\n", payload.c_str());
    
    // Blink green LED to indicate publish
    digitalWrite(LED_PUBLISH, HIGH);
    delay(100);
    digitalWrite(LED_PUBLISH, LOW);
  } else {
    Serial.println("✗ Publish failed\n");
  }
}

bool sendBinaryPublish(const String& topic, const String& payload) {
  if (!client.connected()) {
    return false;
  }
  
  // Binary frame format:
  // [version:1][command:1][topic_len:2][topic][payload_len:4][payload]
  
  uint8_t buffer[512];
  size_t pos = 0;
  
  // Version
  buffer[pos++] = BINARY_PROTOCOL_VERSION;
  
  // Command
  buffer[pos++] = CMD_PUBLISH;
  
  // Topic length (big-endian, 2 bytes)
  uint16_t topic_len = topic.length();
  buffer[pos++] = (topic_len >> 8) & 0xFF;
  buffer[pos++] = topic_len & 0xFF;
  
  // Topic
  memcpy(&buffer[pos], topic.c_str(), topic_len);
  pos += topic_len;
  
  // Payload length (big-endian, 4 bytes)
  uint32_t payload_len = payload.length();
  buffer[pos++] = (payload_len >> 24) & 0xFF;
  buffer[pos++] = (payload_len >> 16) & 0xFF;
  buffer[pos++] = (payload_len >> 8) & 0xFF;
  buffer[pos++] = payload_len & 0xFF;
  
  // Payload
  memcpy(&buffer[pos], payload.c_str(), payload_len);
  pos += payload_len;
  
  // Send frame
  size_t written = client.write(buffer, pos);
  return written == pos;
}
