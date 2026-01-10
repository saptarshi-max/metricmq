// MetricMQ.cpp - ESP32/Arduino Client Library Implementation

#include "MetricMQ.h"

MetricMQClient::MetricMQClient() 
    : port_(6379)
    , use_ip_(false)
    , state_(ConnectionState::DISCONNECTED)
    , exactly_once_enabled_(true)
    , last_sequence_(0)
    , keep_alive_ms_(60000)  // 60 seconds
    , last_activity_(0)
    , last_ping_(0)
    , recv_buffer_pos_(0) {
    
    // Generate default client ID
    uint64_t chipid = ESP.getEfuseMac();
    client_id_ = "esp32_" + String((uint32_t)chipid, HEX);
}

bool MetricMQClient::begin(const char* host, uint16_t port) {
    host_ = String(host);
    port_ = port;
    use_ip_ = false;
    return true;
}

bool MetricMQClient::begin(IPAddress ip, uint16_t port) {
    ip_ = ip;
    port_ = port;
    use_ip_ = true;
    return true;
}

bool MetricMQClient::connect() {
    return connect(client_id_.c_str());
}

bool MetricMQClient::connect(const char* clientId) {
    if (state_ == ConnectionState::CONNECTED) {
        return true;
    }
    
    client_id_ = String(clientId);
    state_ = ConnectionState::CONNECTING;
    
    // Connect to broker
    bool connected = false;
    if (use_ip_) {
        connected = client_.connect(ip_, port_);
    } else {
        connected = client_.connect(host_.c_str(), port_);
    }
    
    if (!connected) {
        state_ = ConnectionState::ERROR;
        return false;
    }
    
    state_ = ConnectionState::CONNECTED;
    updateLastActivity();
    last_ping_ = millis();
    recv_buffer_pos_ = 0;
    
    Serial.printf("[MetricMQ] Connected to broker at %s:%d\n", 
                  use_ip_ ? ip_.toString().c_str() : host_.c_str(), port_);
    Serial.printf("[MetricMQ] Client ID: %s\n", client_id_.c_str());
    
    return true;
}

void MetricMQClient::disconnect() {
    if (client_.connected()) {
        client_.stop();
    }
    resetConnection();
    Serial.println("[MetricMQ] Disconnected");
}

bool MetricMQClient::isConnected() {
    return state_ == ConnectionState::CONNECTED && client_.connected();
}

ConnectionState MetricMQClient::getState() {
    return state_;
}

bool MetricMQClient::publish(const String& topic, const String& payload) {
    return publish(topic, (const uint8_t*)payload.c_str(), payload.length());
}

bool MetricMQClient::publish(const String& topic, const uint8_t* payload, size_t length) {
    if (!isConnected()) {
        return false;
    }
    return sendPublish(topic, payload, length);
}

bool MetricMQClient::subscribe(const String& topic) {
    return subscribe(topic, nullptr);
}

bool MetricMQClient::subscribe(const String& topic, SubscriptionCallback callback) {
    if (!isConnected()) {
        return false;
    }
    
    if (callback) {
        callback_ = callback;
    }
    
    return sendSubscribe(topic);
}

bool MetricMQClient::unsubscribe(const String& topic) {
    if (!isConnected()) {
        return false;
    }
    return sendUnsubscribe(topic);
}

void MetricMQClient::loop() {
    if (!client_.connected()) {
        if (state_ == ConnectionState::CONNECTED) {
            resetConnection();
        }
        return;
    }
    
    // Handle incoming data
    handleIncomingData();
    
    // Send periodic ping (keep-alive)
    unsigned long now = millis();
    if (now - last_ping_ >= keep_alive_ms_) {
        sendPing();
        last_ping_ = now;
    }
    
    // Check for timeout
    checkConnection();
}

void MetricMQClient::setClientId(const char* clientId) {
    client_id_ = String(clientId);
}

const char* MetricMQClient::getClientId() const {
    return client_id_.c_str();
}

void MetricMQClient::setKeepAlive(uint32_t seconds) {
    keep_alive_ms_ = seconds * 1000;
}

void MetricMQClient::setCallback(SubscriptionCallback callback) {
    callback_ = callback;
}

void MetricMQClient::enableExactlyOnce(bool enable) {
    exactly_once_enabled_ = enable;
}

bool MetricMQClient::isExactlyOnceEnabled() const {
    return exactly_once_enabled_;
}

// ========== Protocol Implementation ==========

bool MetricMQClient::sendFrame(BinaryCommand cmd, const String& topic, 
                               const uint8_t* payload, size_t payload_len, uint64_t sequence) {
    if (!client_.connected()) {
        return false;
    }
    
    // Frame format:
    // [version:1][command:1][topic_len:2][topic][payload_len:4][payload][sequence:8]
    
    uint8_t buffer[1024];
    size_t pos = 0;
    
    // Version
    buffer[pos++] = BINARY_PROTOCOL_VERSION;
    
    // Command
    buffer[pos++] = static_cast<uint8_t>(cmd);
    
    // Topic length (2 bytes, big-endian)
    uint16_t topic_len = topic.length();
    buffer[pos++] = (topic_len >> 8) & 0xFF;
    buffer[pos++] = topic_len & 0xFF;
    
    // Topic
    memcpy(&buffer[pos], topic.c_str(), topic_len);
    pos += topic_len;
    
    // Payload length (4 bytes, big-endian)
    buffer[pos++] = (payload_len >> 24) & 0xFF;
    buffer[pos++] = (payload_len >> 16) & 0xFF;
    buffer[pos++] = (payload_len >> 8) & 0xFF;
    buffer[pos++] = payload_len & 0xFF;
    
    // Payload (if any)
    if (payload && payload_len > 0) {
        if (payload_len > sizeof(buffer) - pos - 8) {
            Serial.println("[MetricMQ] ERROR: Payload too large");
            return false;
        }
        memcpy(&buffer[pos], payload, payload_len);
        pos += payload_len;
    }
    
    // Sequence (8 bytes, big-endian) - only for MESSAGE and ACK
    if (cmd == BinaryCommand::MESSAGE || cmd == BinaryCommand::ACK) {
        buffer[pos++] = (sequence >> 56) & 0xFF;
        buffer[pos++] = (sequence >> 48) & 0xFF;
        buffer[pos++] = (sequence >> 40) & 0xFF;
        buffer[pos++] = (sequence >> 32) & 0xFF;
        buffer[pos++] = (sequence >> 24) & 0xFF;
        buffer[pos++] = (sequence >> 16) & 0xFF;
        buffer[pos++] = (sequence >> 8) & 0xFF;
        buffer[pos++] = sequence & 0xFF;
    }
    
    // Send frame
    size_t written = client_.write(buffer, pos);
    updateLastActivity();
    
    return written == pos;
}

bool MetricMQClient::sendSubscribe(const String& topic) {
    // For exactly-once, embed client_id in topic: "client_id\0topic"
    String full_topic = topic;
    if (exactly_once_enabled_) {
        full_topic = client_id_ + String('\0') + topic;
    }
    
    bool success = sendFrame(BinaryCommand::SUBSCRIBE, full_topic);
    if (success) {
        Serial.printf("[MetricMQ] Subscribed to: %s\n", topic.c_str());
    }
    return success;
}

bool MetricMQClient::sendUnsubscribe(const String& topic) {
    bool success = sendFrame(BinaryCommand::UNSUBSCRIBE, topic);
    if (success) {
        Serial.printf("[MetricMQ] Unsubscribed from: %s\n", topic.c_str());
    }
    return success;
}

bool MetricMQClient::sendPublish(const String& topic, const uint8_t* payload, size_t length) {
    bool success = sendFrame(BinaryCommand::PUBLISH, topic, payload, length);
    if (success) {
        Serial.printf("[MetricMQ] Published to %s: %d bytes\n", topic.c_str(), length);
    }
    return success;
}

bool MetricMQClient::sendAck(uint64_t sequence) {
    bool success = sendFrame(BinaryCommand::ACK, "", nullptr, 0, sequence);
    if (success) {
        Serial.printf("[MetricMQ] Sent ACK for seq=%llu\n", sequence);
    }
    return success;
}

bool MetricMQClient::sendPing() {
    return sendFrame(BinaryCommand::PING, "");
}

void MetricMQClient::handleIncomingData() {
    while (client_.available()) {
        if (recv_buffer_pos_ >= sizeof(recv_buffer_)) {
            Serial.println("[MetricMQ] ERROR: Receive buffer overflow, resetting");
            recv_buffer_pos_ = 0;
        }
        
        int byte = client_.read();
        if (byte < 0) break;
        
        recv_buffer_[recv_buffer_pos_++] = (uint8_t)byte;
        updateLastActivity();
        
        // Try to parse frame
        if (parseFrame()) {
            // Frame successfully parsed, continue
        }
    }
}

bool MetricMQClient::parseFrame() {
    // Minimum frame size: version(1) + cmd(1) + topic_len(2) + payload_len(4) = 8 bytes
    if (recv_buffer_pos_ < 8) {
        return false;
    }
    
    size_t pos = 0;
    
    // Version
    uint8_t version = recv_buffer_[pos++];
    if (version != BINARY_PROTOCOL_VERSION) {
        Serial.printf("[MetricMQ] ERROR: Unknown protocol version: 0x%02X\n", version);
        recv_buffer_pos_ = 0;
        return false;
    }
    
    // Command
    BinaryCommand cmd = static_cast<BinaryCommand>(recv_buffer_[pos++]);
    
    // Topic length
    uint16_t topic_len = (recv_buffer_[pos] << 8) | recv_buffer_[pos + 1];
    pos += 2;
    
    // Check if we have enough data for topic
    if (recv_buffer_pos_ < pos + topic_len + 4) {
        return false;  // Need more data
    }
    
    // Topic
    String topic((char*)&recv_buffer_[pos], topic_len);
    pos += topic_len;
    
    // Payload length
    uint32_t payload_len = (recv_buffer_[pos] << 24) | (recv_buffer_[pos + 1] << 16) |
                          (recv_buffer_[pos + 2] << 8) | recv_buffer_[pos + 3];
    pos += 4;
    
    // Sequence (for MESSAGE frames)
    uint64_t sequence = 0;
    size_t expected_size = pos + payload_len;
    if (cmd == BinaryCommand::MESSAGE) {
        expected_size += 8;  // Add sequence bytes
    }
    
    // Check if we have complete frame
    if (recv_buffer_pos_ < expected_size) {
        return false;  // Need more data
    }
    
    // Payload
    const uint8_t* payload = &recv_buffer_[pos];
    pos += payload_len;
    
    // Sequence (if MESSAGE)
    if (cmd == BinaryCommand::MESSAGE) {
        sequence = ((uint64_t)recv_buffer_[pos] << 56) |
                  ((uint64_t)recv_buffer_[pos + 1] << 48) |
                  ((uint64_t)recv_buffer_[pos + 2] << 40) |
                  ((uint64_t)recv_buffer_[pos + 3] << 32) |
                  ((uint64_t)recv_buffer_[pos + 4] << 24) |
                  ((uint64_t)recv_buffer_[pos + 5] << 16) |
                  ((uint64_t)recv_buffer_[pos + 6] << 8) |
                  ((uint64_t)recv_buffer_[pos + 7]);
        pos += 8;
    }
    
    // Process message
    if (cmd == BinaryCommand::MESSAGE) {
        processMessage(topic, payload, payload_len, sequence);
    }
    
    // Remove processed frame from buffer
    size_t remaining = recv_buffer_pos_ - pos;
    if (remaining > 0) {
        memmove(recv_buffer_, &recv_buffer_[pos], remaining);
    }
    recv_buffer_pos_ = remaining;
    
    return true;
}

void MetricMQClient::processMessage(const String& topic, const uint8_t* payload, 
                                    size_t length, uint64_t sequence) {
    Serial.printf("[MetricMQ] Received message on '%s': %d bytes (seq=%llu)\n", 
                  topic.c_str(), length, sequence);
    
    // Call user callback
    if (callback_) {
        callback_(topic, payload, length, sequence);
    }
    
    // Send ACK if exactly-once is enabled
    if (exactly_once_enabled_ && sequence > 0) {
        sendAck(sequence);
    }
    
    last_sequence_ = sequence;
}

void MetricMQClient::resetConnection() {
    state_ = ConnectionState::DISCONNECTED;
    recv_buffer_pos_ = 0;
    last_sequence_ = 0;
}

void MetricMQClient::updateLastActivity() {
    last_activity_ = millis();
}

bool MetricMQClient::checkConnection() {
    unsigned long now = millis();
    unsigned long timeout = keep_alive_ms_ * 3;  // 3x keep-alive = timeout
    
    if (now - last_activity_ > timeout) {
        Serial.println("[MetricMQ] Connection timeout");
        resetConnection();
        return false;
    }
    
    return true;
}
