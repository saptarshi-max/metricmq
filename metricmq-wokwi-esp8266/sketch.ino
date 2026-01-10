/*
 * MetricMQ ESP8266 Wokwi Demo with DHT22
 * 
 * This demonstrates MetricMQ on ESP8266 with:
 * - DHT22 temperature & humidity sensor
 * - Built-in LED for connection status
 * - Real sensor readings
 * - Binary protocol communication
 * 
 * Hardware (simulated in Wokwi):
 * - ESP8266 NodeMCU
 * - DHT22 sensor on GPIO 2 (D4)
 * - Built-in LED on GPIO 2
 * 
 * Setup Instructions:
 * 1. Start MetricMQ broker on your machine: ./metricmq-broker
 * 2. Click "Start Simulation" in Wokwi
 * 3. Watch the LED and serial monitor
 * 4. Adjust DHT22 temperature/humidity in simulator
 * 
 * Expected Behavior:
 * - Built-in LED stays ON when connected to broker
 * - LED blinks every time sensor data is published (10 sec)
 * - Serial monitor shows real-time status and sensor readings
 * - Broker receives ESP8266 sensor updates
 */

#include <ESP8266WiFi.h>
#include "DHTesp.h"

// Pin definitions
#define DHT_PIN 2  // GPIO 2 (D4 on NodeMCU)

// Configuration
const char* WIFI_SSID = "Wokwi-GUEST";  // Wokwi's default WiFi
const char* WIFI_PASS = "";
const char* BROKER_IP = "host.wokwi.internal";  // Connect to host machine
const uint16_t BROKER_PORT = 6379;

WiFiClient tcpClient;
DHTesp dht;

// Timing
unsigned long lastPublish = 0;
const unsigned long PUBLISH_INTERVAL = 10000;  // 10 seconds
// Simple RESP protocol helper
void sendRESP(const String& command) {
  tcpClient.print(command);
  Serial.print("SENT: " + command);
}

// PUBLISH command in RESP format
String makePublish(const String& topic, const String& payload) {
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

  // Initialize built-in LED (active LOW)
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);  // LED OFF initially

  Serial.println("\n=== MetricMQ ESP8266 DHT22 Client ===");

  // Initialize DHT sensor
  dht.setup(DHT_PIN, DHTesp::DHT22);
  Serial.println("✓ DHT22 sensor initialized");

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
    digitalWrite(LED_BUILTIN, LOW);  // LED ON

    // Subscribe to commands topic
    sendRESP(makeSubscribe("esp8266/commands"));

  } else {
    Serial.println("❌ Connection failed!");
    Serial.println("Check broker IP and port");
  }
}

void loop() {
  // Publish sensor data every 10 seconds
  static unsigned long lastPublish = 0;

  if (millis() - lastPublish > 10000) {
    if (tcpClient.connected()) {
      publishSensorData();
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
    digitalWrite(LED_BUILTIN, HIGH);  // LED OFF
    delay(5000);
    ESP.restart();  // Reconnect
  }

  delay(100);
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
  payload += "\"uptime\":" + String(millis() / 1000) + ",";
  payload += "\"chip_id\":\"" + String(ESP.getChipId(), HEX) + "\"";
  payload += "}";
  
  // Print sensor readings
  Serial.println("\n┌─────────────────────────────────────┐");
  Serial.printf("│ 🌡️  Temperature: %5.1f°C         │\n", data.temperature);
  Serial.printf("│ 💧 Humidity:    %5.1f%%          │\n", data.humidity);
  Serial.printf("│ ⏱️  Uptime:      %5lu sec       │\n", millis() / 1000);
  Serial.println("└─────────────────────────────────────┘");
  
  sendRESP(makePublish("sensors/esp8266/dht22", payload));
  
  Serial.println("📤 Published sensor data");
  
  // Blink LED to show publish
  digitalWrite(LED_BUILTIN, HIGH);  // LED OFF
  delay(200);
  digitalWrite(LED_BUILTIN, LOW);   // LED ON
}