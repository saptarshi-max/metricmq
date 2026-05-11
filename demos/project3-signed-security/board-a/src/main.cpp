// ============================================================
// Project 3 — Signed Secure Publisher (Board A)
// ============================================================
// Signs every message with an Ed25519 secret key.
// Publishes to: secure/sensors/temperature
//
// At the 30-second mark, deliberately sends an UNSIGNED
// message to a secure/ topic. The broker rejects it.
// Board B never sees it. That's the whole point.
//
// BEFORE FLASHING:
//   1. Run metricmq-keygen on your laptop to generate keys
//   2. Paste the 64-byte secret key into SECRET_KEY below
//   3. Register the public key in the broker's src/main.cpp
// ============================================================

#include <WiFi.h>
#include <MetricMQ.h>

// ============ CONFIGURE THESE ============
const char* WIFI_SSID     = "YourWiFi";
const char* WIFI_PASSWORD = "YourPassword";
const char* BROKER_HOST   = "192.168.29.168";  // Your laptop's IP
const uint16_t BROKER_PORT = 6379;
// =========================================

// ============ PASTE YOUR KEY HERE ============
// Run: .\build\Release\metricmq-keygen.exe "sensor_node_1" "secure/sensors/*"
// Paste the 64-byte DEVICE_SECRET_KEY array from the output below.
const uint8_t SECRET_KEY[64] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // ← REPLACE
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  //    THESE
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  //    WITH
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  //    YOUR
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  //    KEYGEN
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  //    OUTPUT
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  //    (64
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00   //    BYTES)
};
const uint32_t KEY_ID = 1;
// =============================================

MetricMQClient client;
unsigned long lastPublish = 0;
uint32_t msgCount = 0;
bool triedUnsigned = false;

void setup() {
    Serial.begin(115200);
    delay(500);
    randomSeed(analogRead(0));

    Serial.println("\n╔══════════════════════════════════════════╗");
    Serial.println("║  Project 3 — Signed Secure Publisher      ║");
    Serial.println("║  Board A · Ed25519 · ESP32-S3 N16R8      ║");
    Serial.println("╚══════════════════════════════════════════╝\n");

    if (psramFound()) {
        Serial.printf("PSRAM: %u bytes available\n", ESP.getPsramSize());
    }

    // Check if keys are still placeholder zeros
    bool keysSet = false;
    for (int i = 0; i < 64; i++) {
        if (SECRET_KEY[i] != 0x00) { keysSet = true; break; }
    }
    if (!keysSet) {
        Serial.println("!!! WARNING: SECRET_KEY is all zeros !!!");
        Serial.println("!!! Run metricmq-keygen and paste the output !!!");
        Serial.println("!!! See README.md for instructions !!!\n");
    }

    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.print("Connecting to WiFi");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.printf("\nConnected! IP: %s\n", WiFi.localIP().toString().c_str());

    // Set the signing key BEFORE connecting
    client.setSigningKey(SECRET_KEY, KEY_ID);

    client.begin(BROKER_HOST, BROKER_PORT);
    client.connect("secure-sensor-A");
    Serial.printf("Connected. Ed25519 signing ENABLED. Key ID: %u\n\n", KEY_ID);
    Serial.println("Sending signed messages every 5s to secure/sensors/temperature");
    Serial.println("At 30s mark: will attempt UNSIGNED publish (broker should reject)\n");
}

void loop() {
    if (!client.isConnected()) {
        Serial.println("[!] Reconnecting...");
        client.connect("secure-sensor-A");
    }
    client.loop();

    if (millis() - lastPublish >= 5000) {
        lastPublish = millis();
        msgCount++;

        float temp = 20.0 + (random(0, 100) / 10.0);
        String payload = "{\"temp\":" + String(temp, 1) +
                         ",\"msg\":" + String(msgCount) +
                         ",\"heap\":" + String(ESP.getFreeHeap()) + "}";

        // Signed publish — broker will accept this
        unsigned long t0 = micros();
        client.publishSigned("secure/sensors/temperature", payload);
        unsigned long signTime = micros() - t0;

        Serial.printf("[SIGNED #%u] secure/sensors/temperature -> %s (%luus)\n",
                      msgCount, payload.c_str(), signTime);

        // At the 30-second mark, try an UNSIGNED publish to a secure topic.
        // The broker REJECTS this — Board B never sees it.
        if (millis() > 30000 && !triedUnsigned) {
            triedUnsigned = true;
            Serial.println("\n╔══════════════════════════════════════════════════╗");
            Serial.println("║  >>> SENDING UNSIGNED MESSAGE TO secure/ TOPIC   ║");
            Serial.println("║  >>> Broker should REJECT this.                  ║");
            Serial.println("║  >>> Board B should see NOTHING.                 ║");
            Serial.println("╚══════════════════════════════════════════════════╝\n");
            client.publish("secure/sensors/temperature", "{\"fake\":true}");
            Serial.println("[UNSIGNED] Sent. Check broker console and Board B.\n");
        }
    }
}
