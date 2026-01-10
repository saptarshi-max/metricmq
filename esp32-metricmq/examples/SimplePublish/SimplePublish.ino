/*
 * ESP32 MetricMQ Example: Simple Publish
 * 
 * This example demonstrates how to connect to a MetricMQ broker
 * and publish sensor data at regular intervals.
 * 
 * Hardware Requirements:
 * - ESP32 board
 * - WiFi connection
 * 
 * Circuit:
 * - No additional hardware required for this basic example
 * 
 * Instructions:
 * 1. Update WIFI_SSID and WIFI_PASSWORD with your credentials
 * 2. Update BROKER_HOST with your MetricMQ broker address
 * 3. Upload to ESP32
 * 4. Open Serial Monitor (115200 baud)
 */

#include <WiFi.h>
#include <MetricMQ.h>

// WiFi credentials
const char* WIFI_SSID = "YourWiFiSSID";
const char* WIFI_PASSWORD = "YourWiFiPassword";

// MetricMQ broker configuration
const char* BROKER_HOST = "192.168.1.100";  // Your broker IP
const uint16_t BROKER_PORT = 6379;

// Create MetricMQ client
MetricMQClient mqClient;

// Timing
unsigned long lastPublish = 0;
const unsigned long PUBLISH_INTERVAL = 5000;  // Publish every 5 seconds

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n=== MetricMQ ESP32 Publisher ===");
  
  // Connect to WiFi
  Serial.printf("Connecting to WiFi: %s\n", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  Serial.println("\nWiFi connected!");
  Serial.printf("IP address: %s\n", WiFi.localIP().toString().c_str());
  
  // Initialize MetricMQ client
  mqClient.begin(BROKER_HOST, BROKER_PORT);
  mqClient.setClientId("esp32_publisher");
  
  // Connect to broker
  Serial.printf("Connecting to MetricMQ broker at %s:%d\n", BROKER_HOST, BROKER_PORT);
  if (mqClient.connect()) {
    Serial.println("✓ Connected to MetricMQ broker");
  } else {
    Serial.println("✗ Failed to connect to broker");
  }
}

void loop() {
  // Keep connection alive
  mqClient.loop();
  
  // Reconnect if disconnected
  if (!mqClient.isConnected()) {
    Serial.println("Reconnecting to broker...");
    if (mqClient.connect()) {
      Serial.println("✓ Reconnected");
    } else {
      delay(5000);  // Wait before retry
      return;
    }
  }
  
  // Publish sensor data periodically
  unsigned long now = millis();
  if (now - lastPublish >= PUBLISH_INTERVAL) {
    lastPublish = now;
    
    // Simulate sensor readings
    float temperature = 20.0 + random(-50, 50) / 10.0;  // 15-25°C
    float humidity = 50.0 + random(-100, 100) / 10.0;   // 40-60%
    int rssi = WiFi.RSSI();
    
    // Create JSON payload
    String payload = "{";
    payload += "\"temperature\":" + String(temperature, 1) + ",";
    payload += "\"humidity\":" + String(humidity, 1) + ",";
    payload += "\"rssi\":" + String(rssi) + ",";
    payload += "\"uptime\":" + String(millis() / 1000) + ",";
    payload += "\"heap\":" + String(ESP.getFreeHeap());
    payload += "}";
    
    // Publish to topic
    Serial.printf("Publishing to 'sensors/esp32': %s\n", payload.c_str());
    if (mqClient.publish("sensors/esp32", payload)) {
      Serial.println("✓ Published successfully");
    } else {
      Serial.println("✗ Publish failed");
    }
  }
}
