// ============================================================
// Project 3 — Verifying Subscriber (Board B)
// ============================================================
// Subscribes to: secure/#
// Verifies Ed25519 signatures LOCALLY using Board A's
// public key. Does NOT just trust the broker.
//
// BEFORE FLASHING:
//   1. Paste Board A's 32-byte PUBLIC key below
//   2. This is the public half — safe to distribute
// ============================================================

#include <WiFi.h>
#include <MetricMQ.h>

// ============ CONFIGURE THESE ============
const char* WIFI_SSID     = "YourWiFi";
const char* WIFI_PASSWORD = "YourPassword";
const char* BROKER_HOST   = "192.168.29.168";  // Your laptop's IP
const uint16_t BROKER_PORT = 6379;
// =========================================

// ============ PASTE BOARD A's PUBLIC KEY HERE ============
// This is the 32-byte PUBLIC key from metricmq-keygen output.
// It is safe to share — this is NOT the secret key.
const uint8_t PUBLISHER_PUBLIC_KEY[32] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // ← REPLACE
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  //    WITH
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  //    YOUR
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00   //    KEY
};
const uint32_t PUBLISHER_KEY_ID = 1;
// =========================================================

MetricMQClient client;
uint32_t verified = 0;
uint32_t received = 0;

void onSecureMessage(const String& topic, const uint8_t* payload,
                     size_t len, uint64_t seq) {
    received++;
    String data;
    data.reserve(len);
    for (size_t i = 0; i < len; i++) data += (char)payload[i];

    // Messages that arrive here passed broker verification.
    // The client also verifies locally if addVerifyKey was called.
    verified++;

    Serial.printf("[VERIFIED seq=%llu #%u] %s -> %s\n",
                  seq, received, topic.c_str(), data.c_str());
    Serial.printf("  Stats: %u verified, %u total\n", verified, received);
}

void setup() {
    Serial.begin(115200);
    delay(500);

    Serial.println("\n╔══════════════════════════════════════════╗");
    Serial.println("║  Project 3 — Verifying Subscriber         ║");
    Serial.println("║  Board B · Local Ed25519 Verification     ║");
    Serial.println("╚══════════════════════════════════════════╝\n");

    if (psramFound()) {
        Serial.printf("PSRAM: %u bytes available\n", ESP.getPsramSize());
    }

    // Check if key is still placeholder zeros
    bool keySet = false;
    for (int i = 0; i < 32; i++) {
        if (PUBLISHER_PUBLIC_KEY[i] != 0x00) { keySet = true; break; }
    }
    if (!keySet) {
        Serial.println("!!! WARNING: PUBLISHER_PUBLIC_KEY is all zeros !!!");
        Serial.println("!!! Paste Board A's public key from keygen output !!!");
        Serial.println("!!! See README.md for instructions !!!\n");
    }

    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.print("Connecting to WiFi");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.printf("\nConnected! IP: %s\n", WiFi.localIP().toString().c_str());

    // Register Board A's public key for local verification
    client.addVerifyKey(PUBLISHER_KEY_ID, PUBLISHER_PUBLIC_KEY);
    Serial.printf("Registered publisher key_id=%u for local verification\n",
                  PUBLISHER_KEY_ID);

    client.begin(BROKER_HOST, BROKER_PORT);
    client.connect("verifier-B");

    client.subscribe("secure/#", onSecureMessage);
    Serial.println("Subscribed to secure/#");
    Serial.println("Waiting for signed messages...\n");
    Serial.println("NOTE: Unsigned publishes to secure/ topics are rejected");
    Serial.println("by the broker — they will NEVER appear here.\n");
}

void loop() {
    if (!client.isConnected()) {
        Serial.println("[!] Reconnecting...");
        client.connect("verifier-B");
        client.subscribe("secure/#", onSecureMessage);
    }
    client.loop();
}
