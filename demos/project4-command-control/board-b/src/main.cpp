// Project 4 — Board B: Control Panel (Receives Data, Sends Commands)
// Subscribes to devices/board-a/telemetry and devices/board-a/status
// Publishes commands to commands/board-a on a 15-second cycle
// Command sequence: LED_ON -> INTERVAL:1000 -> STATUS -> INTERVAL:5000 -> LED_OFF -> repeat

#include <WiFi.h>
#include <MetricMQ.h>

// ============ CONFIGURE THESE ============
const char* WIFI_SSID     = "YourWiFi";
const char* WIFI_PASSWORD = "YourPassword";
const char* BROKER_HOST   = "192.168.29.168";
// =========================================

MetricMQClient client;
unsigned long lastCommand = 0;
uint8_t commandPhase = 0;
uint32_t telemetryCount = 0;

const char* COMMAND_NAMES[] = {
    "LED_ON",
    "INTERVAL:1000",
    "STATUS",
    "INTERVAL:5000",
    "LED_OFF"
};
const uint8_t NUM_COMMANDS = 5;

void onTelemetry(const String& topic, const uint8_t* payload,
                 size_t len, uint64_t seq) {
    String data;
    data.reserve(len);
    for (size_t i = 0; i < len; i++) data += (char)payload[i];

    telemetryCount++;
    Serial.printf("[TELEMETRY seq=%llu #%u] %s\n", seq, telemetryCount, data.c_str());
}

void onStatus(const String& topic, const uint8_t* payload,
              size_t len, uint64_t seq) {
    String data;
    data.reserve(len);
    for (size_t i = 0; i < len; i++) data += (char)payload[i];

    Serial.println();
    Serial.println("  ┌─────────────────────────────────────────┐");
    Serial.printf( "  │ STATUS REPORT from Board A               │\n");
    Serial.println("  ├─────────────────────────────────────────┤");
    Serial.printf( "  │ %s\n", data.c_str());
    Serial.println("  └─────────────────────────────────────────┘");
    Serial.println();
}

void setup() {
    Serial.begin(115200);
    delay(500);

    Serial.println();
    Serial.println("================================================");
    Serial.println("  Project 4 - Control Panel (Sub + Pub)");
    Serial.println("  Board B - ESP32-S3 N16R8");
    Serial.println("  Watches telemetry, sends commands every 15s");
    Serial.println("================================================");

    if (psramFound()) {
        Serial.printf("PSRAM: %u bytes available\n", ESP.getFreePsram());
    }

    Serial.print("Connecting to WiFi");
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.printf("\nConnected! IP: %s\n", WiFi.localIP().toString().c_str());

    client.begin(BROKER_HOST, 6379);
    if (client.connect("control-panel-B")) {
        Serial.println("Connected to MetricMQ broker");
    }

    client.subscribe("devices/board-a/telemetry", onTelemetry);
    client.subscribe("devices/board-a/status", onStatus);

    Serial.println("Subscribed to Board A telemetry + status");
    Serial.println();
    Serial.println("Command sequence (every 15s):");
    Serial.println("  1. LED_ON");
    Serial.println("  2. INTERVAL:1000 (speed up publishing)");
    Serial.println("  3. STATUS (request report)");
    Serial.println("  4. INTERVAL:5000 (normal speed)");
    Serial.println("  5. LED_OFF");
    Serial.println("  Then repeats...\n");
}

void loop() {
    if (!client.isConnected()) {
        Serial.println("[RECONNECT] Attempting...");
        if (client.connect("control-panel-B")) {
            client.subscribe("devices/board-a/telemetry", onTelemetry);
            client.subscribe("devices/board-a/status", onStatus);
            Serial.println("[RECONNECT] Success, re-subscribed");
        }
    }
    client.loop();

    if (millis() - lastCommand >= 15000) {
        lastCommand = millis();

        if (commandPhase >= NUM_COMMANDS) {
            Serial.println("\n>> Command cycle complete. Restarting...\n");
            commandPhase = 0;
        }

        const char* cmd = COMMAND_NAMES[commandPhase];
        Serial.printf("\n>> [Phase %u/5] Sending: %s\n", commandPhase + 1, cmd);
        client.publish("commands/board-a", cmd);
        commandPhase++;
    }
}
