// ============================================================
// Project 2 — Exactly-Once Data Logger (Board B)
// ============================================================
// Subscribes to: sensors/temperature
// Logs every reading with its broker-assigned sequence number.
// Detects gaps (missed messages) and duplicates.
//
// THE EXPERIMENT:
//   1. Let both boards run for ~30 seconds.
//   2. Unplug Board B's USB cable.
//   3. Wait 20 seconds (Board A keeps publishing).
//   4. Plug Board B back in.
//   5. Board B resumes from the exact sequence it left off.
//      No gaps, no duplicates — that's exactly-once.
//
// The magic: client_id "logger-B" tells the broker to track
// this subscriber's last ACKed sequence in LMDB. On reconnect,
// the broker replays from last_ack + 1.
// ============================================================

#include <WiFi.h>
#include <MetricMQ.h>

// ============ CONFIGURE THESE ============
const char* WIFI_SSID     = "YourWiFi";
const char* WIFI_PASSWORD = "YourPassword";
const char* BROKER_HOST   = "192.168.29.168";  // Your laptop's IP
const uint16_t BROKER_PORT = 6379;
// =========================================

MetricMQClient client;
uint64_t lastSeq = 0;
uint32_t totalReceived = 0;
uint32_t gapCount = 0;
uint32_t dupCount = 0;

void onReading(const String& topic, const uint8_t* payload,
               size_t len, uint64_t seq) {
    totalReceived++;
    String data;
    data.reserve(len);
    for (size_t i = 0; i < len; i++) data += (char)payload[i];

    // Check for gaps or duplicates
    if (lastSeq > 0) {
        if (seq == lastSeq) {
            dupCount++;
            Serial.printf("[DUPLICATE] seq=%llu — this should NEVER happen!\n", seq);
            return;
        }
        if (seq != lastSeq + 1) {
            gapCount++;
            Serial.printf("[GAP] expected seq=%llu, got seq=%llu (missed %llu)\n",
                          lastSeq + 1, seq, seq - lastSeq - 1);
        }
    }
    lastSeq = seq;

    Serial.printf("[seq=%llu total=%u] %s\n", seq, totalReceived, data.c_str());

    // Print stats every 10 messages
    if (totalReceived % 10 == 0) {
        Serial.println("─────────────────────────────────────────");
        Serial.printf("  Stats: %u received, %u gaps, %u duplicates\n",
                      totalReceived, gapCount, dupCount);
        Serial.println("─────────────────────────────────────────");
    }
}

void setup() {
    Serial.begin(115200);
    delay(500);

    Serial.println("\n╔══════════════════════════════════════╗");
    Serial.println("║  Project 2 — Exactly-Once Logger     ║");
    Serial.println("║  Board B · Disconnect & Replay Demo  ║");
    Serial.println("╚══════════════════════════════════════╝\n");

    Serial.println(">>> EXPERIMENT: Unplug me mid-stream, wait 20s,");
    Serial.println(">>> plug back in, and watch the gap-free replay!\n");

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

    // IMPORTANT: "logger-B" is the client_id that enables replay-on-reconnect.
    // The broker tracks the last ACKed sequence for this ID in LMDB.
    client.begin(BROKER_HOST, BROKER_PORT);
    client.connect("logger-B");

    client.subscribe("sensors/temperature", onReading);
    Serial.println("Subscribed to sensors/temperature\n");
}

void loop() {
    if (!client.isConnected()) {
        Serial.println("\n[!] Reconnecting — expecting replay from last ACK...");
        client.connect("logger-B");
        client.subscribe("sensors/temperature", onReading);
    }
    client.loop();
}
