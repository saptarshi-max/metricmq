// ============================================================
// Project 1 — Heartbeat Publisher (Board A)
// ============================================================
// Publishes a heartbeat every 3 seconds with:
//   - beat count, uptime, free heap, WiFi RSSI
// Topic: devices/board-a/heartbeat
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
unsigned long lastHeartbeat = 0;
uint32_t beatCount = 0;

void setup() {
    Serial.begin(115200);
    delay(500);

    Serial.println("\n╔══════════════════════════════════════╗");
    Serial.println("║  Project 1 — Heartbeat Publisher     ║");
    Serial.println("║  Board A · ESP32-S3 N16R8            ║");
    Serial.println("╚══════════════════════════════════════╝\n");

    // Report PSRAM
    if (psramFound()) {
        Serial.printf("PSRAM: %u bytes available\n", ESP.getPsramSize());
    }

    // Connect WiFi
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.print("Connecting to WiFi");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.printf("\nConnected! IP: %s\n", WiFi.localIP().toString().c_str());

    // Connect to broker with a client ID (enables exactly-once tracking)
    client.begin(BROKER_HOST, BROKER_PORT);
    client.connect("heartbeat-node-A");
    Serial.println("Connected to MetricMQ broker\n");
    Serial.println("Publishing heartbeat every 3 seconds...\n");
}

void loop() {
    // Reconnect if dropped
    if (!client.isConnected()) {
        Serial.println("[!] Reconnecting...");
        client.connect("heartbeat-node-A");
    }
    client.loop();

    // Publish heartbeat every 3 seconds
    if (millis() - lastHeartbeat >= 3000) {
        lastHeartbeat = millis();
        beatCount++;

        // Build a JSON payload with useful telemetry
        String payload = "{";
        payload += "\"beat\":" + String(beatCount);
        payload += ",\"uptime_s\":" + String(millis() / 1000);
        payload += ",\"heap\":" + String(ESP.getFreeHeap());
        payload += ",\"rssi\":" + String(WiFi.RSSI());
        if (psramFound()) {
            payload += ",\"psram_free\":" + String(ESP.getFreePsram());
        }
        payload += "}";

        client.publish("devices/board-a/heartbeat", payload);
        Serial.printf("[beat #%u] heap=%u rssi=%d\n",
                      beatCount, ESP.getFreeHeap(), WiFi.RSSI());
    }
}
