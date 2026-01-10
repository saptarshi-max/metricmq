# MetricMQ ESP32 Arduino Library

A lightweight, high-performance MQTT-like client library for ESP32 that connects to MetricMQ broker using the binary protocol.

## Features

- ✅ **Binary Protocol** - Efficient custom protocol (40% smaller than RESP)
- ✅ **Exactly-Once Delivery** - Automatic ACK mechanism prevents duplicate messages
- ✅ **Persistent Reconnection** - Auto-reconnect with exponential backoff
- ✅ **Keep-Alive** - Automatic ping/pong for connection health
- ✅ **Lightweight** - Minimal memory footprint (~2KB RAM)
- ✅ **Simple API** - Easy to use, Arduino-style interface
- ✅ **Callback Support** - Message callbacks for reactive programming
- ✅ **WiFi Auto-Connect** - Handles WiFi reconnection automatically

## Installation

### Arduino IDE

1. Download the library as ZIP
2. Open Arduino IDE
3. Go to **Sketch → Include Library → Add .ZIP Library**
4. Select the downloaded `esp32-metricmq.zip`
5. Restart Arduino IDE

### PlatformIO

Add to `platformio.ini`:

```ini
[env:esp32]
platform = espressif32
framework = arduino
lib_deps = 
    https://github.com/yourusername/esp32-metricmq.git
```

## Quick Start

### Simple Publisher

```cpp
#include <WiFi.h>
#include <MetricMQ.h>

MetricMQClient mqClient;

void setup() {
  WiFi.begin("SSID", "password");
  while (WiFi.status() != WL_CONNECTED) delay(500);
  
  mqClient.begin("192.168.1.100", 6379);
  mqClient.connect();
}

void loop() {
  mqClient.loop();
  
  String payload = "{\"temperature\":23.5}";
  mqClient.publish("sensors/temp", payload);
  
  delay(5000);
}
```

### Simple Subscriber

```cpp
#include <WiFi.h>
#include <MetricMQ.h>

MetricMQClient mqClient;

void onMessage(const String& topic, const uint8_t* payload, 
               size_t length, uint64_t sequence) {
  Serial.printf("Received on %s: ", topic.c_str());
  for (size_t i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();
}

void setup() {
  WiFi.begin("SSID", "password");
  while (WiFi.status() != WL_CONNECTED) delay(500);
  
  mqClient.begin("192.168.1.100", 6379);
  mqClient.setCallback(onMessage);
  mqClient.connect();
  mqClient.subscribe("sensors/temp");
}

void loop() {
  mqClient.loop();
}
```

## API Reference

### Initialization

```cpp
MetricMQClient client;

// Initialize with hostname
client.begin("broker.local", 6379);

// Initialize with IP address
IPAddress ip(192, 168, 1, 100);
client.begin(ip, 6379);
```

### Connection

```cpp
// Connect with auto-generated client ID
client.connect();

// Connect with custom client ID
client.connect("esp32_sensor_01");

// Disconnect
client.disconnect();

// Check connection status
if (client.isConnected()) {
  // Connected
}
```

### Publishing

```cpp
// Publish string
client.publish("topic/name", "Hello, MetricMQ!");

// Publish binary data
uint8_t data[] = {0x01, 0x02, 0x03};
client.publish("topic/binary", data, sizeof(data));
```

### Subscribing

```cpp
// Subscribe without callback (use global callback)
client.subscribe("topic/name");

// Subscribe with specific callback
client.subscribe("topic/name", [](const String& topic, 
                                   const uint8_t* payload, 
                                   size_t length, 
                                   uint64_t sequence) {
  // Handle message
});

// Unsubscribe
client.unsubscribe("topic/name");
```

### Configuration

```cpp
// Set client ID
client.setClientId("my_esp32_device");

// Get client ID
const char* id = client.getClientId();

// Set keep-alive interval (seconds)
client.setKeepAlive(60);

// Enable/disable exactly-once delivery
client.enableExactlyOnce(true);

// Set global message callback
client.setCallback(onMessageCallback);
```

### Main Loop

```cpp
void loop() {
  // Must be called regularly to:
  // - Process incoming messages
  // - Send keep-alive pings
  // - Handle reconnection
  client.loop();
}
```

## Examples

The library includes several examples:

1. **SimplePublish** - Basic publishing example
2. **SimpleSubscribe** - Basic subscribing example with LED blink
3. **DHT22Sensor** - Real-world temperature & humidity sensor

See `examples/` folder for complete code.

## Hardware Requirements

- **ESP32** (any variant)
- **ESP8266** (supported, but ESP32 recommended)
- WiFi connection

## Memory Usage

- **Flash:** ~15 KB
- **RAM:** ~2 KB (idle), ~4 KB (active)
- **Buffer:** 2 KB receive buffer (configurable)

## Exactly-Once Delivery

MetricMQ supports exactly-once delivery semantics:

```cpp
// Enable exactly-once
client.enableExactlyOnce(true);

// Subscribe - client_id is automatically embedded
client.subscribe("sensors/temp");

// Messages are automatically ACK'd after callback
```

How it works:
1. Each message has a unique sequence ID
2. Client automatically sends ACK after processing
3. Broker tracks ACK'd messages per client
4. On reconnect, only unACK'd messages are replayed

## Troubleshooting

### Connection fails

```cpp
if (!client.connect()) {
  Serial.println("Connection failed!");
  Serial.printf("State: %d\n", (int)client.getState());
}
```

### Messages not received

1. Check `client.loop()` is called in main loop
2. Verify callback is set: `client.setCallback(myCallback)`
3. Check broker is running and accessible
4. Verify topic name matches exactly

### Memory issues

Reduce buffer size in `MetricMQ.h`:
```cpp
uint8_t recv_buffer_[1024];  // Reduce from 2048
```

## Protocol Details

### Frame Format

```
[version:1][command:1][topic_len:2][topic][payload_len:4][payload][sequence:8]
```

### Commands

- `0x01` - SUBSCRIBE
- `0x02` - UNSUBSCRIBE
- `0x03` - PUBLISH
- `0x04` - MESSAGE (broker → client)
- `0x05` - PING
- `0x06` - ACK

## License

MIT License - See LICENSE file for details

## Contributing

Contributions welcome! Please open an issue or pull request.

## Support

- GitHub Issues: https://github.com/yourusername/esp32-metricmq/issues
- Documentation: https://metricmq.io/docs
- Forum: https://community.metricmq.io
