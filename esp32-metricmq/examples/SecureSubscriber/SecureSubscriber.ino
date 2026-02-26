/*
 * MetricMQ Security Demo: Verifying Subscriber (ESP32-S3 Board #2)
 *
 * HEADLINE FEATURE: Receives Ed25519-signed messages and VERIFIES them locally.
 * If a signature fails verification, the message is REJECTED client-side.
 * This is true end-to-end security — not just transport encryption.
 *
 * Demonstrates:
 *   - Ed25519 signature verification (built-in libsodium)
 *   - Signed message reception (SIGNED_MESSAGE = 0x11)
 *   - Local verification toggle (vs trusting broker)
 *   - RGB LED feedback (color = message type & verification status)
 *   - Unsigned message reception for comparison
 *   - Per-topic statistics and duplicate detection
 *   - Auto-reconnect, keep-alive
 *
 * Hardware: ESP32-S3-DevKitC-1-N16R8 (RGB LED on GPIO 48)
 *
 * SETUP:
 *   1. Paste Board #1's PUBLIC key below (from keygen output)
 *   2. Set WiFi credentials and broker IP
 *   3. Upload to Board #2
 */

#include <WiFi.h>
#include <MetricMQ.h>

// ===== CONFIGURATION =====
const char* WIFI_SSID      = "YourWiFiSSID";
const char* WIFI_PASSWORD   = "YourWiFiPassword";
const char* BROKER_HOST     = "192.168.1.100";
const uint16_t BROKER_PORT  = 6379;

// ===== PUBLISHER'S PUBLIC KEY (get this from Board #1 keygen output) =====
// This is the PUBLIC key of the sensor_node_1.
// The subscriber needs it to VERIFY signatures locally.
const uint32_t PUBLISHER_KEY_ID = 1;  // Must match Board #1's key ID

// PLACEHOLDER — replace with Board #1's public key!
const uint8_t PUBLISHER_PUBLIC_KEY[32] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

// LED
#ifdef RGB_BUILTIN
  #define LED_PIN RGB_BUILTIN
#else
  #define LED_PIN 48
#endif

MetricMQClient mqClient;

// Statistics
struct TopicStats {
    String topic;
    uint32_t received;
    uint32_t verified;
    uint32_t failed;
    uint32_t lastSeq;
    uint32_t duplicates;
    unsigned long totalVerifyUs;
};

static const int MAX_TOPICS = 16;
TopicStats stats[MAX_TOPICS];
int topicCount = 0;

uint32_t totalSigned = 0;
uint32_t totalUnsigned = 0;
uint32_t totalVerified = 0;
uint32_t totalFailed = 0;

unsigned long lastStats = 0;

// Find or create topic stats entry
TopicStats& getStats(const String& topic) {
    for (int i = 0; i < topicCount; i++) {
        if (stats[i].topic == topic) return stats[i];
    }
    if (topicCount < MAX_TOPICS) {
        stats[topicCount].topic = topic;
        stats[topicCount].received = 0;
        stats[topicCount].verified = 0;
        stats[topicCount].failed = 0;
        stats[topicCount].lastSeq = 0;
        stats[topicCount].duplicates = 0;
        stats[topicCount].totalVerifyUs = 0;
        return stats[topicCount++];
    }
    return stats[0];  // fallback
}

// Callback: ordinary (unsigned) message
void onMessage(const String& topic, const String& payload) {
    totalUnsigned++;
    TopicStats& ts = getStats(topic);
    ts.received++;

    Serial.printf("[MSG] %s -> %s\n", topic.c_str(), payload.c_str());

    // Brief green blink for unsigned
    blinkLED(20);
}

// Callback: signed message (with signature + key_id metadata)
void onSignedMessage(const String& topic, const String& payload,
                     const uint8_t* signature, uint32_t keyId, bool verified) {
    totalSigned++;
    TopicStats& ts = getStats(topic);
    ts.received++;

    if (verified) {
        totalVerified++;
        ts.verified++;
        Serial.printf("[SIGNED ✓ ] key_id=%u  %s -> %s\n",
                      keyId, topic.c_str(), payload.c_str());
        // Long blue blink for verified
        blinkLED(80);
    } else {
        totalFailed++;
        ts.failed++;
        Serial.printf("[SIGNED ✗ ] key_id=%u  VERIFICATION FAILED  %s\n",
                      keyId, topic.c_str());
        // Rapid red blink for failure
        for (int i = 0; i < 5; i++) {
            blinkLED(30);
            delay(30);
        }
    }

    // Duplicate detection via seq in JSON
    int seqIdx = payload.indexOf("\"seq\":");
    if (seqIdx >= 0) {
        uint32_t seq = (uint32_t)payload.substring(seqIdx + 6).toInt();
        if (seq > 0 && seq == ts.lastSeq) {
            ts.duplicates++;
            Serial.printf("  [DUP] seq %u on topic %s\n", seq, topic.c_str());
        }
        if (seq > ts.lastSeq) ts.lastSeq = seq;
    }
}

void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println("\n╔══════════════════════════════════════════════════╗");
    Serial.println("║  MetricMQ SECURITY Demo — VERIFYING SUBSCRIBER   ║");
    Serial.println("║  Ed25519 Verification | Split Trust | ESP32-S3   ║");
    Serial.println("╚══════════════════════════════════════════════════╝\n");

    if (psramFound()) {
        Serial.printf("PSRAM: %u bytes available\n", ESP.getPsramSize());
    }

    // LED
    pinMode(LED_PIN, OUTPUT);

    // WiFi
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.printf("Connecting to WiFi '%s'", WIFI_SSID);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.printf(" Connected! IP: %s\n", WiFi.localIP().toString().c_str());

    // MetricMQ setup
    mqClient.begin(BROKER_HOST, BROKER_PORT);
    mqClient.setClientId("esp32s3_secure_sub");
    mqClient.enableExactlyOnce(true);
    mqClient.setKeepAlive(15);

    // Register publisher's public key for LOCAL verification
    mqClient.addVerificationKey(PUBLISHER_KEY_ID, PUBLISHER_PUBLIC_KEY);
    mqClient.enableLocalVerification(true);
    Serial.println(">>> Ed25519 verification: ENABLED (local)");
    Serial.printf(">>> Registered publisher key_id: %u\n", PUBLISHER_KEY_ID);

    // Set callbacks
    mqClient.setCallback(onMessage);
    mqClient.setSignedCallback(onSignedMessage);

    // Connect and subscribe
    Serial.printf("Connecting to broker at %s:%d...\n", BROKER_HOST, BROKER_PORT);
    if (mqClient.connect()) {
        Serial.println(">>> CONNECTED (binary protocol v1)");

        // Subscribe to all secure topics
        mqClient.subscribe("secure/#");
        Serial.println(">>> Subscribed: secure/#");

        // Subscribe to unsigned topics for comparison
        mqClient.subscribe("sensors/#");
        Serial.println(">>> Subscribed: sensors/#");

        // Subscribe to system topics
        mqClient.subscribe("demo/#");
        Serial.println(">>> Subscribed: demo/#");
    } else {
        Serial.println("!!! Connection failed — retrying in loop()");
    }

    Serial.println("\n--- Waiting for messages ---\n");
}

void loop() {
    mqClient.loop();

    if (!mqClient.isConnected()) {
        Serial.println("Reconnecting...");
        if (mqClient.connect()) {
            Serial.println(">>> Reconnected! Re-subscribing...");
            mqClient.subscribe("secure/#");
            mqClient.subscribe("sensors/#");
            mqClient.subscribe("demo/#");
        } else {
            delay(3000);
            return;
        }
    }

    unsigned long now = millis();

    // Print stats every 10 seconds
    if (now - lastStats >= 10000) {
        lastStats = now;
        printStats();
    }
}

void printStats() {
    Serial.println("\n┌──── Verifying Subscriber Stats ────────────────────────┐");
    Serial.printf("│ Total signed msgs:     %u\n", totalSigned);
    Serial.printf("│   Verified (✓):        %u\n", totalVerified);
    Serial.printf("│   Failed (✗):          %u\n", totalFailed);
    Serial.printf("│ Total unsigned msgs:   %u\n", totalUnsigned);
    Serial.printf("│ Free heap:             %u bytes\n", ESP.getFreeHeap());
    if (psramFound()) {
        Serial.printf("│ PSRAM free:            %u bytes\n", ESP.getFreePsram());
    }
    Serial.printf("│ WiFi RSSI:             %d dBm\n", WiFi.RSSI());
    Serial.printf("│ Uptime:                %lus\n", millis() / 1000);

    if (topicCount > 0) {
        Serial.println("├──── Per-Topic Breakdown ───────────────────────────────┤");
        for (int i = 0; i < topicCount; i++) {
            Serial.printf("│ %-35s rcvd=%u ok=%u fail=%u dup=%u\n",
                          stats[i].topic.c_str(),
                          stats[i].received,
                          stats[i].verified,
                          stats[i].failed,
                          stats[i].duplicates);
        }
    }
    Serial.println("└────────────────────────────────────────────────────────┘\n");
}

void blinkLED(int ms) {
    digitalWrite(LED_PIN, HIGH);
    delay(ms);
    digitalWrite(LED_PIN, LOW);
}
