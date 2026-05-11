// ============================================================
// Project 2 — Exactly-Once Sensor Publisher (Board A)
// ============================================================
// Publishes simulated temperature readings every 2 seconds.
// Each reading has an incrementing count so Board B can verify
// no messages were lost or duplicated after a disconnect.
// Topic: sensors/temperature
// ============================================================

#include <WiFi.h>
#include <MetricMQ.h>

// ============ CONFIGURE THESE ============
const char* WIFI_SSID     = "S.Nag_4G";
const char* WIFI_PASSWORD = "subhashish245";
const char* BROKER_HOST   = "192.168.29.168";  // Your laptop's IP
const uint16_t BROKER_PORT = 6379;
// =========================================

MetricMQClient client;
unsigned long lastPublish = 0;
uint32_t readingNum = 0;

// Simulate temperature (random walk between 20-30°C)
float simulateTemp() {
    static float temp = 25.0;
    temp += (random(-10, 11) / 10.0);
    temp = constrain(temp, 20.0, 30.0);
    return temp;
}

void setup() {
    Serial.begin(115200);
    delay(500);
    randomSeed(analogRead(0));

    Serial.println("\n╔══════════════════════════════════════╗");
    Serial.println("║  Project 2 — Sensor Publisher        ║");
    Serial.println("║  Board A · Exactly-Once Demo         ║");
    Serial.println("╚══════════════════════════════════════╝\n");

    if (psramFound()) {
        Serial.printf("PSRAM: %u bytes available\n", ESP.getPsramSize());
    }

    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.print("Connecting to WiFi");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.printf("\nConnected! IP: %s\n", WiFi.localIP().toString().c_str());

    client.begin(BROKER_HOST, BROKER_PORT);
    client.connect("sensor-A");
    Serial.println("Connected to MetricMQ broker");
    Serial.println("Publishing to sensors/temperature every 2s\n");
}

void loop() {
    if (!client.isConnected()) {
        Serial.println("[!] Reconnecting...");
        client.connect("sensor-A");
    }
    client.loop();

    if (millis() - lastPublish >= 2000) {
        lastPublish = millis();
        readingNum++;

        float temp = simulateTemp();
        String payload = "{\"n\":" + String(readingNum) +
                         ",\"temp\":" + String(temp, 1) +
                         ",\"heap\":" + String(ESP.getFreeHeap()) + "}";

        client.publish("sensors/temperature", payload);
        Serial.printf("[#%u] %.1f°C  (heap=%u)\n",
                      readingNum, temp, ESP.getFreeHeap());
    }
}
