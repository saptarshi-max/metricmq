/*
 * ESP32 MetricMQ Example: Simple Subscribe
 * 
 * This example demonstrates how to connect to a MetricMQ broker
 * and subscribe to a topic to receive messages.
 * 
 * Hardware Requirements:
 * - ESP32 board
 * - WiFi connection
 * - Optional: LED on GPIO 2 (built-in on most ESP32 boards)
 * 
 * Circuit:
 * - LED connected to GPIO 2 (or use built-in LED)
 * 
 * Instructions:
 * 1. Update WIFI_SSID and WIFI_PASSWORD with your credentials
 * 2. Update BROKER_HOST with your MetricMQ broker address
 * 3. Upload to ESP32
 * 4. Open Serial Monitor (115200 baud)
 * 5. Publish messages to 'sensors/esp32' from another client
 */

#include <WiFi.h>
#include <MetricMQ.h>

// WiFi credentials
const char* WIFI_SSID = "YourWiFiSSID";
const char* WIFI_PASSWORD = "YourWiFiPassword";

// MetricMQ broker configuration
const char* BROKER_HOST = "192.168.1.100";  // Your broker IP
const uint16_t BROKER_PORT = 6379;

// GPIO pin for LED
const int LED_PIN = 2;  // Built-in LED on most ESP32 boards

// Create MetricMQ client
MetricMQClient mqClient;

// Message counter
int messageCount = 0;

// Callback function for received messages
void onMessage(const String& topic, const uint8_t* payload, size_t length, uint64_t sequence) {
  messageCount++;
  
  Serial.println("\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
  Serial.printf("📨 Message #%d received\n", messageCount);
  Serial.printf("📍 Topic: %s\n", topic.c_str());
  Serial.printf("🔢 Sequence: %llu\n", sequence);
  Serial.printf("📏 Length: %d bytes\n", length);
  Serial.print("📄 Payload: ");
  
  // Print payload as string
  for (size_t i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();
  Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
  
  // Blink LED to indicate message received
  digitalWrite(LED_PIN, HIGH);
  delay(100);
  digitalWrite(LED_PIN, LOW);
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n=== MetricMQ ESP32 Subscriber ===");
  
  // Setup LED
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  
  // Connect to WiFi
  Serial.printf("Connecting to WiFi: %s\n", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    // Blink LED while connecting
    digitalWrite(LED_PIN, !digitalRead(LED_PIN));
  }
  
  digitalWrite(LED_PIN, LOW);
  Serial.println("\nWiFi connected!");
  Serial.printf("IP address: %s\n", WiFi.localIP().toString().c_str());
  
  // Initialize MetricMQ client
  mqClient.begin(BROKER_HOST, BROKER_PORT);
  mqClient.setClientId("esp32_subscriber");
  mqClient.setCallback(onMessage);
  
  // Enable exactly-once delivery
  mqClient.enableExactlyOnce(true);
  
  // Connect to broker
  Serial.printf("Connecting to MetricMQ broker at %s:%d\n", BROKER_HOST, BROKER_PORT);
  if (mqClient.connect()) {
    Serial.println("✓ Connected to MetricMQ broker");
    
    // Subscribe to topic
    Serial.println("Subscribing to topic: sensors/esp32");
    if (mqClient.subscribe("sensors/esp32")) {
      Serial.println("✓ Subscribed successfully");
      Serial.println("\nWaiting for messages...");
      
      // Blink LED 3 times to indicate ready
      for (int i = 0; i < 3; i++) {
        digitalWrite(LED_PIN, HIGH);
        delay(200);
        digitalWrite(LED_PIN, LOW);
        delay(200);
      }
    } else {
      Serial.println("✗ Subscribe failed");
    }
  } else {
    Serial.println("✗ Failed to connect to broker");
  }
}

void loop() {
  // Keep connection alive and process incoming messages
  mqClient.loop();
  
  // Reconnect if disconnected
  if (!mqClient.isConnected()) {
    Serial.println("Reconnecting to broker...");
    if (mqClient.connect()) {
      Serial.println("✓ Reconnected");
      mqClient.subscribe("sensors/esp32");
    } else {
      delay(5000);  // Wait before retry
    }
  }
}
