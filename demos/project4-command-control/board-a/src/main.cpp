// Project 4 — Board A: Sensor Node (Publishes Data, Receives Commands)
// Publishes telemetry to devices/board-a/telemetry
// Subscribes to commands/board-a for remote commands
// Supports: LED_ON, LED_OFF, INTERVAL:xxxx, STATUS

#include <WiFi.h>
#include <MetricMQ.h>

// ============ CONFIGURE THESE ============
const char* WIFI_SSID     = "YourWiFi";
const char* WIFI_PASSWORD = "YourPassword";
const char* BROKER_HOST   = "192.168.29.168";
// =========================================

#define LED_PIN 2  // Built-in LED on most ESP32-S3 boards

MetricMQClient client;
unsigned long lastPublish = 0;
unsigned long publishInterval = 5000;  // Starts at 5 seconds
bool ledState = false;
uint32_t msgCount = 0;

void onCommand(const String& topic, const uint8_t* payload,
               size_t len, uint64_t seq) {
    String cmd;
    cmd.reserve(len);
    for (size_t i = 0; i < len; i++) cmd += (char)payload[i];

    Serial.printf("\n[COMMAND seq=%llu] %s -> %s\n", seq, topic.c_str(), cmd.c_str());

    if (cmd == "LED_ON") {
        digitalWrite(LED_PIN, HIGH);
        ledState = true;
        Serial.println("  -> LED turned ON");
    }
    else if (cmd == "LED_OFF") {
        digitalWrite(LED_PIN, LOW);
        ledState = false;
        Serial.println("  -> LED turned OFF");
    }
    else if (cmd.startsWith("INTERVAL:")) {
        unsigned long newInterval = cmd.substring(9).toInt();
        if (newInterval >= 500 && newInterval <= 60000) {
            publishInterval = newInterval;
            Serial.printf("  -> Publish interval changed to %lums\n", publishInterval);
        } else {
            Serial.printf("  -> Invalid interval %lu (must be 500-60000)\n", newInterval);
        }
    }
    else if (cmd == "STATUS") {
        String status = "{\"led\":" + String(ledState ? "true" : "false") +
                        ",\"interval\":" + String(publishInterval) +
                        ",\"heap\":" + String(ESP.getFreeHeap()) +
                        ",\"rssi\":" + String(WiFi.RSSI()) +
                        ",\"uptime\":" + String(millis() / 1000) +
                        ",\"msgs_sent\":" + String(msgCount) + "}";
        client.publish("devices/board-a/status", status);
        Serial.println("  -> Status report sent to devices/board-a/status");
    }
    else {
        Serial.printf("  -> Unknown command: %s\n", cmd.c_str());
    }
}

void setup() {
    Serial.begin(115200);
    delay(500);
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);
    randomSeed(analogRead(0));

    Serial.println();
    Serial.println("================================================");
    Serial.println("  Project 4 - Sensor Node (Pub + Sub)");
    Serial.println("  Board A - ESP32-S3 N16R8");
    Serial.println("  Publishes telemetry, receives commands");
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
    if (client.connect("sensor-node-A")) {
        Serial.println("Connected to MetricMQ broker");
    }

    client.subscribe("commands/board-a", onCommand);
    Serial.println("Listening for commands on: commands/board-a");
    Serial.printf("Publishing telemetry every %lums to: devices/board-a/telemetry\n\n", publishInterval);
}

void loop() {
    if (!client.isConnected()) {
        Serial.println("[RECONNECT] Attempting...");
        if (client.connect("sensor-node-A")) {
            client.subscribe("commands/board-a", onCommand);
            Serial.println("[RECONNECT] Success, re-subscribed to commands");
        }
    }
    client.loop();

    if (millis() - lastPublish >= publishInterval) {
        lastPublish = millis();
        msgCount++;
        float temp = 22.0 + (random(0, 80) / 10.0);

        String payload = "{\"temp\":" + String(temp, 1) +
                         ",\"led\":" + String(ledState ? "true" : "false") +
                         ",\"interval\":" + String(publishInterval) +
                         ",\"msg\":" + String(msgCount) +
                         ",\"heap\":" + String(ESP.getFreeHeap()) + "}";

        client.publish("devices/board-a/telemetry", payload);
        Serial.printf("[PUB #%u] temp=%.1f led=%s interval=%lu heap=%u\n",
                      msgCount, temp, ledState ? "ON" : "OFF",
                      publishInterval, ESP.getFreeHeap());
    }
}
