// ============================================================
// Project 1 — Heartbeat Monitor (Board B)
// ============================================================
// Subscribes to: devices/board-a/heartbeat
// Raises an alarm if no heartbeat arrives within 10 seconds.
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
unsigned long lastReceived = 0;
bool alarmActive = false;
uint32_t totalReceived = 0;

void onHeartbeat(const String& topic, const uint8_t* payload,
                 size_t len, uint64_t seq) {
    lastReceived = millis();
    alarmActive = false;
    totalReceived++;

    // Print the received heartbeat
    String data;
    data.reserve(len);
    for (size_t i = 0; i < len; i++) data += (char)payload[i];

    Serial.printf("[seq=%llu #%u] %s: %s\n",
                  seq, totalReceived, topic.c_str(), data.c_str());
}

void setup() {
    Serial.begin(115200);
    delay(500);

    Serial.println("\n╔══════════════════════════════════════╗");
    Serial.println("║  Project 1 — Heartbeat Monitor       ║");
    Serial.println("║  Board B · ESP32-S3 N16R8            ║");
    Serial.println("╚══════════════════════════════════════╝\n");

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

    // Connect with a different client ID
    client.begin(BROKER_HOST, BROKER_PORT);
    client.connect("monitor-node-B");

    // Subscribe to Board A's heartbeat topic
    client.subscribe("devices/board-a/heartbeat", onHeartbeat);
    Serial.println("Subscribed to devices/board-a/heartbeat");
    Serial.println("Waiting for heartbeats (alarm after 10s of silence)...\n");

    lastReceived = millis();
}

void loop() {
    if (!client.isConnected()) {
        Serial.println("[!] Reconnecting...");
        client.connect("monitor-node-B");
        client.subscribe("devices/board-a/heartbeat", onHeartbeat);
    }
    client.loop();

    // Check for timeout — no heartbeat in 10 seconds
    if (millis() - lastReceived > 10000 && !alarmActive) {
        alarmActive = true;
        Serial.println("\n!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
        Serial.println("!!! ALARM: Board A heartbeat lost for 10+ s !!!");
        Serial.println("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
    }
}
