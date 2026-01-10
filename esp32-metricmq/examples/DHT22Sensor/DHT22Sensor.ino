/*
 * ESP32 MetricMQ Example: DHT22 Temperature & Humidity Sensor
 * 
 * This example reads temperature and humidity from a DHT22 sensor
 * and publishes the data to a MetricMQ broker.
 * 
 * Hardware Requirements:
 * - ESP32 board
 * - DHT22 (or DHT11) temperature & humidity sensor
 * - 10kΩ resistor (pull-up for DHT22 data line)
 * - WiFi connection
 * 
 * Circuit:
 * - DHT22 VCC -> ESP32 3.3V
 * - DHT22 GND -> ESP32 GND
 * - DHT22 DATA -> ESP32 GPIO 4 (with 10kΩ pull-up to 3.3V)
 * 
 * Library Dependencies:
 * - DHT sensor library by Adafruit
 * - Adafruit Unified Sensor
 * 
 * Instructions:
 * 1. Install DHT sensor library via Arduino Library Manager
 * 2. Update WIFI_SSID and WIFI_PASSWORD with your credentials
 * 3. Update BROKER_HOST with your MetricMQ broker address
 * 4. Connect DHT22 to GPIO 4
 * 5. Upload to ESP32
 * 6. Open Serial Monitor (115200 baud)
 */

#include <WiFi.h>
#include <MetricMQ.h>
#include <DHT.h>

// WiFi credentials
const char* WIFI_SSID = "YourWiFiSSID";
const char* WIFI_PASSWORD = "YourWiFiPassword";

// MetricMQ broker configuration
const char* BROKER_HOST = "192.168.1.100";
const uint16_t BROKER_PORT = 6379;

// DHT22 configuration
#define DHT_PIN 4
#define DHT_TYPE DHT22  // Change to DHT11 if using DHT11

// Create instances
MetricMQClient mqClient;
DHT dht(DHT_PIN, DHT_TYPE);

// Timing
unsigned long lastPublish = 0;
const unsigned long PUBLISH_INTERVAL = 10000;  // Publish every 10 seconds

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n=== MetricMQ ESP32 DHT22 Sensor ===");
  
  // Initialize DHT sensor
  dht.begin();
  Serial.println("DHT22 sensor initialized");
  
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
  
  // Generate unique client ID using MAC address
  uint64_t chipid = ESP.getEfuseMac();
  String clientId = "esp32_dht22_" + String((uint32_t)chipid, HEX);
  mqClient.setClientId(clientId.c_str());
  
  // Connect to broker
  Serial.printf("Connecting to MetricMQ broker at %s:%d\n", BROKER_HOST, BROKER_PORT);
  if (mqClient.connect()) {
    Serial.println("✓ Connected to MetricMQ broker");
    Serial.printf("Client ID: %s\n", mqClient.getClientId());
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
      delay(5000);
      return;
    }
  }
  
  // Publish sensor data periodically
  unsigned long now = millis();
  if (now - lastPublish >= PUBLISH_INTERVAL) {
    lastPublish = now;
    
    // Read sensor data
    float humidity = dht.readHumidity();
    float temperature = dht.readTemperature();
    
    // Check if reading failed
    if (isnan(humidity) || isnan(temperature)) {
      Serial.println("✗ Failed to read from DHT sensor!");
      return;
    }
    
    // Calculate heat index
    float heatIndex = dht.computeHeatIndex(temperature, humidity, false);
    
    // Get WiFi signal strength
    int rssi = WiFi.RSSI();
    
    // Create JSON payload
    String payload = "{";
    payload += "\"temperature\":" + String(temperature, 1) + ",";
    payload += "\"humidity\":" + String(humidity, 1) + ",";
    payload += "\"heat_index\":" + String(heatIndex, 1) + ",";
    payload += "\"rssi\":" + String(rssi) + ",";
    payload += "\"uptime\":" + String(millis() / 1000) + ",";
    payload += "\"client_id\":\"" + String(mqClient.getClientId()) + "\"";
    payload += "}";
    
    // Print sensor readings
    Serial.println("\n━━━━━━━━━━━━━━━━━━━━━━━━━━━");
    Serial.printf("🌡️  Temperature: %.1f°C\n", temperature);
    Serial.printf("💧 Humidity: %.1f%%\n", humidity);
    Serial.printf("🔥 Heat Index: %.1f°C\n", heatIndex);
    Serial.printf("📶 RSSI: %d dBm\n", rssi);
    Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━");
    
    // Publish to topic
    if (mqClient.publish("sensors/dht22", payload)) {
      Serial.println("✓ Published to sensors/dht22");
    } else {
      Serial.println("✗ Publish failed");
    }
  }
}
