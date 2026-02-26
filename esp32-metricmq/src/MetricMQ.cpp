// MetricMQ.cpp - ESP32/Arduino Client Library Implementation
// Wire-compatible with desktop broker (16-byte header format)
// Ed25519 signing via ESP32's built-in libsodium (ships with arduino-esp32 core)

#include "MetricMQ.h"

// ESP32 Arduino core includes libsodium as a component
// Available since arduino-esp32 v2.0.0+
#include "sodium.h"

// ====================================================================
// MetricMQClient implementation
// ====================================================================

MetricMQClient::MetricMQClient()
    : port_(6379)
    , use_ip_(false)
    , state_(ConnectionState::DISCONNECTED)
    , exactly_once_enabled_(true)
    , last_sequence_(0)
    , keep_alive_ms_(60000)
    , last_activity_(0)
    , last_ping_(0)
    , recv_buffer_pos_(0)
    , signing_enabled_(false)
    , signing_key_id_(0)
    , local_verify_enabled_(false)
    , verify_key_count_(0) {

    uint64_t chipid = ESP.getEfuseMac();
    client_id_ = "esp32_" + String((uint32_t)chipid, HEX);
    memset(secret_key_, 0, sizeof(secret_key_));
    memset(verify_keys_, 0, sizeof(verify_keys_));
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
    if (state_ == ConnectionState::CONNECTED) return true;

    client_id_ = String(clientId);
    state_ = ConnectionState::CONNECTING;

    bool connected = use_ip_ ? client_.connect(ip_, port_)
                             : client_.connect(host_.c_str(), port_);
    if (!connected) {
        state_ = ConnectionState::ERROR;
        return false;
    }

    state_ = ConnectionState::CONNECTED;
    updateLastActivity();
    last_ping_ = millis();
    recv_buffer_pos_ = 0;

    Serial.printf("[MetricMQ] Connected to %s:%d\n",
                  use_ip_ ? ip_.toString().c_str() : host_.c_str(), port_);
    Serial.printf("[MetricMQ] Client ID: %s | Signing: %s\n",
                  client_id_.c_str(), signing_enabled_ ? "ON" : "OFF");
    return true;
}

void MetricMQClient::disconnect() {
    if (client_.connected()) client_.stop();
    resetConnection();
    Serial.println("[MetricMQ] Disconnected");
}

bool MetricMQClient::isConnected() {
    return state_ == ConnectionState::CONNECTED && client_.connected();
}

ConnectionState MetricMQClient::getState() { return state_; }

// ===== Unsigned messaging =====

bool MetricMQClient::publish(const String& topic, const String& payload) {
    return publish(topic, (const uint8_t*)payload.c_str(), payload.length());
}

bool MetricMQClient::publish(const String& topic, const uint8_t* payload, size_t length) {
    if (!isConnected()) return false;
    return sendPublish(topic, payload, length);
}

bool MetricMQClient::subscribe(const String& topic) {
    return subscribe(topic, nullptr);
}

bool MetricMQClient::subscribe(const String& topic, SubscriptionCallback callback) {
    if (!isConnected()) return false;
    if (callback) callback_ = callback;
    return sendSubscribe(topic);
}

bool MetricMQClient::unsubscribe(const String& topic) {
    if (!isConnected()) return false;
    return sendUnsubscribe(topic);
}

// ===== Ed25519 Signed Messaging =====

void MetricMQClient::setSigningKey(const uint8_t* secret_key, uint32_t key_id) {
    memcpy(secret_key_, secret_key, METRICMQ_SECRET_KEY_SIZE);
    signing_key_id_ = key_id;
    signing_enabled_ = true;
    Serial.printf("[MetricMQ] Signing enabled: key_id=%u\n", key_id);
}

void MetricMQClient::addVerificationKey(const uint8_t* public_key, uint32_t key_id) {
    if (verify_key_count_ >= MAX_VERIFY_KEYS) {
        Serial.println("[MetricMQ] WARNING: Max verification keys reached");
        return;
    }
    memcpy(verify_keys_[verify_key_count_].public_key, public_key, METRICMQ_PUBLIC_KEY_SIZE);
    verify_keys_[verify_key_count_].key_id = key_id;
    verify_keys_[verify_key_count_].valid = true;
    verify_key_count_++;
    Serial.printf("[MetricMQ] Added verification key: key_id=%u\n", key_id);
}

bool MetricMQClient::publishSigned(const String& topic, const String& payload) {
    return publishSigned(topic, (const uint8_t*)payload.c_str(), payload.length());
}

bool MetricMQClient::publishSigned(const String& topic, const uint8_t* payload, size_t length) {
    if (!isConnected()) return false;
    if (!signing_enabled_) {
        Serial.println("[MetricMQ] ERROR: Signing not configured (call setSigningKey first)");
        return false;
    }

    // Build message to sign: topic + payload (matches broker verification)
    size_t msg_len = topic.length() + length;
    uint8_t* msg = (uint8_t*)malloc(msg_len);
    if (!msg) {
        Serial.println("[MetricMQ] ERROR: malloc failed for signing");
        return false;
    }
    memcpy(msg, topic.c_str(), topic.length());
    memcpy(msg + topic.length(), payload, length);

    // Sign
    uint8_t signature[METRICMQ_SIGNATURE_SIZE];
    bool ok = ed25519Sign(msg, msg_len, secret_key_, signature);
    free(msg);

    if (!ok) {
        Serial.println("[MetricMQ] ERROR: Ed25519 signing failed");
        return false;
    }

    // Send signed frame
    ok = sendSignedFrame(topic, payload, length, signature, signing_key_id_);
    if (ok) {
        Serial.printf("[MetricMQ] SIGNED publish -> %s (%u bytes, key_id=%u)\n",
                      topic.c_str(), (unsigned)length, signing_key_id_);
    }
    return ok;
}

void MetricMQClient::setSignedCallback(SignedMessageCallback callback) {
    signed_callback_ = callback;
}

bool MetricMQClient::isSigningEnabled() const { return signing_enabled_; }

void MetricMQClient::enableLocalVerification(bool enable) {
    local_verify_enabled_ = enable;
}

// ===== Configuration =====

void MetricMQClient::loop() {
    if (!client_.connected()) {
        if (state_ == ConnectionState::CONNECTED) resetConnection();
        return;
    }
    handleIncomingData();
    unsigned long now = millis();
    if (now - last_ping_ >= keep_alive_ms_) {
        sendPing();
        last_ping_ = now;
    }
    checkConnection();
}

void MetricMQClient::setClientId(const char* clientId) { client_id_ = String(clientId); }
const char* MetricMQClient::getClientId() const { return client_id_.c_str(); }
void MetricMQClient::setKeepAlive(uint32_t seconds) { keep_alive_ms_ = seconds * 1000; }
void MetricMQClient::setCallback(SubscriptionCallback callback) { callback_ = callback; }
void MetricMQClient::enableExactlyOnce(bool enable) { exactly_once_enabled_ = enable; }
bool MetricMQClient::isExactlyOnceEnabled() const { return exactly_once_enabled_; }

// ====================================================================
// Wire-format: Desktop-compatible 16-byte header
// [Version:1][Command:1][Sequence:8][TopicLen:2][PayloadLen:4][Topic][Payload]
// Signed frames append: [Signature:64][KeyID:4]
// ====================================================================

bool MetricMQClient::sendFrame(BinaryCommand cmd, const String& topic,
                               const uint8_t* payload, size_t payload_len,
                               uint64_t sequence) {
    if (!client_.connected()) return false;

    // Calculate total frame size
    uint16_t topic_len = topic.length();
    size_t frame_size = BINARY_HEADER_SIZE + topic_len + payload_len;
    if (frame_size > 1500) {
        Serial.println("[MetricMQ] ERROR: Frame too large");
        return false;
    }

    uint8_t buffer[1500];
    size_t pos = 0;

    // Version
    buffer[pos++] = BINARY_PROTOCOL_VERSION;
    // Command
    buffer[pos++] = static_cast<uint8_t>(cmd);
    // Sequence (8 bytes, big-endian) — ALWAYS present in header
    buffer[pos++] = (sequence >> 56) & 0xFF;
    buffer[pos++] = (sequence >> 48) & 0xFF;
    buffer[pos++] = (sequence >> 40) & 0xFF;
    buffer[pos++] = (sequence >> 32) & 0xFF;
    buffer[pos++] = (sequence >> 24) & 0xFF;
    buffer[pos++] = (sequence >> 16) & 0xFF;
    buffer[pos++] = (sequence >> 8) & 0xFF;
    buffer[pos++] = sequence & 0xFF;
    // Topic length (2 bytes, big-endian)
    buffer[pos++] = (topic_len >> 8) & 0xFF;
    buffer[pos++] = topic_len & 0xFF;
    // Payload length (4 bytes, big-endian)
    buffer[pos++] = (payload_len >> 24) & 0xFF;
    buffer[pos++] = (payload_len >> 16) & 0xFF;
    buffer[pos++] = (payload_len >> 8) & 0xFF;
    buffer[pos++] = payload_len & 0xFF;
    // Topic
    if (topic_len > 0) {
        memcpy(&buffer[pos], topic.c_str(), topic_len);
        pos += topic_len;
    }
    // Payload
    if (payload && payload_len > 0) {
        memcpy(&buffer[pos], payload, payload_len);
        pos += payload_len;
    }

    size_t written = client_.write(buffer, pos);
    updateLastActivity();
    return written == pos;
}

bool MetricMQClient::sendSignedFrame(const String& topic, const uint8_t* payload,
                                     size_t payload_len, const uint8_t* signature,
                                     uint32_t key_id, uint64_t sequence) {
    if (!client_.connected()) return false;

    uint16_t topic_len = topic.length();
    size_t frame_size = BINARY_HEADER_SIZE + topic_len + payload_len
                      + METRICMQ_SIGNATURE_SIZE + METRICMQ_KEY_ID_SIZE;
    if (frame_size > 1500) {
        Serial.println("[MetricMQ] ERROR: Signed frame too large");
        return false;
    }

    uint8_t buffer[1500];
    size_t pos = 0;

    // Header
    buffer[pos++] = BINARY_PROTOCOL_VERSION;
    buffer[pos++] = static_cast<uint8_t>(BinaryCommand::SIGNED_PUBLISH);
    // Sequence (8B)
    buffer[pos++] = (sequence >> 56) & 0xFF;
    buffer[pos++] = (sequence >> 48) & 0xFF;
    buffer[pos++] = (sequence >> 40) & 0xFF;
    buffer[pos++] = (sequence >> 32) & 0xFF;
    buffer[pos++] = (sequence >> 24) & 0xFF;
    buffer[pos++] = (sequence >> 16) & 0xFF;
    buffer[pos++] = (sequence >> 8) & 0xFF;
    buffer[pos++] = sequence & 0xFF;
    // Topic length (2B)
    buffer[pos++] = (topic_len >> 8) & 0xFF;
    buffer[pos++] = topic_len & 0xFF;
    // Payload length (4B)
    buffer[pos++] = (payload_len >> 24) & 0xFF;
    buffer[pos++] = (payload_len >> 16) & 0xFF;
    buffer[pos++] = (payload_len >> 8) & 0xFF;
    buffer[pos++] = payload_len & 0xFF;
    // Topic
    if (topic_len > 0) {
        memcpy(&buffer[pos], topic.c_str(), topic_len);
        pos += topic_len;
    }
    // Payload
    if (payload && payload_len > 0) {
        memcpy(&buffer[pos], payload, payload_len);
        pos += payload_len;
    }
    // Signature (64B)
    memcpy(&buffer[pos], signature, METRICMQ_SIGNATURE_SIZE);
    pos += METRICMQ_SIGNATURE_SIZE;
    // Key ID (4B, big-endian)
    buffer[pos++] = (key_id >> 24) & 0xFF;
    buffer[pos++] = (key_id >> 16) & 0xFF;
    buffer[pos++] = (key_id >> 8) & 0xFF;
    buffer[pos++] = key_id & 0xFF;

    size_t written = client_.write(buffer, pos);
    updateLastActivity();
    return written == pos;
}

bool MetricMQClient::sendSubscribe(const String& topic) {
    String full_topic = topic;
    if (exactly_once_enabled_) {
        full_topic = client_id_ + String('\0') + topic;
    }
    bool ok = sendFrame(BinaryCommand::SUBSCRIBE, full_topic);
    if (ok) Serial.printf("[MetricMQ] Subscribed to: %s\n", topic.c_str());
    return ok;
}

bool MetricMQClient::sendUnsubscribe(const String& topic) {
    bool ok = sendFrame(BinaryCommand::UNSUBSCRIBE, topic);
    if (ok) Serial.printf("[MetricMQ] Unsubscribed from: %s\n", topic.c_str());
    return ok;
}

bool MetricMQClient::sendPublish(const String& topic, const uint8_t* payload, size_t length) {
    bool ok = sendFrame(BinaryCommand::PUBLISH, topic, payload, length);
    if (ok) Serial.printf("[MetricMQ] Published to %s: %d bytes\n", topic.c_str(), length);
    return ok;
}

bool MetricMQClient::sendAck(uint64_t sequence) {
    bool ok = sendFrame(BinaryCommand::ACK, "", nullptr, 0, sequence);
    if (ok) Serial.printf("[MetricMQ] Sent ACK seq=%llu\n", sequence);
    return ok;
}

bool MetricMQClient::sendPing() {
    return sendFrame(BinaryCommand::PING, "");
}

// ====================================================================
// Receive & Parse — Desktop-compatible format
// ====================================================================

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
        parseFrame();  // Try parsing after each byte
    }
}

bool MetricMQClient::parseFrame() {
    // Need at least 16-byte header
    if (recv_buffer_pos_ < BINARY_HEADER_SIZE) return false;

    size_t pos = 0;

    // Version
    uint8_t version = recv_buffer_[pos++];
    if (version != BINARY_PROTOCOL_VERSION) {
        Serial.printf("[MetricMQ] ERROR: Unknown version 0x%02X\n", version);
        recv_buffer_pos_ = 0;
        return false;
    }

    // Command
    BinaryCommand cmd = static_cast<BinaryCommand>(recv_buffer_[pos++]);

    // Sequence (8 bytes, big-endian)
    uint64_t sequence = 0;
    sequence = ((uint64_t)recv_buffer_[pos]   << 56) |
               ((uint64_t)recv_buffer_[pos+1] << 48) |
               ((uint64_t)recv_buffer_[pos+2] << 40) |
               ((uint64_t)recv_buffer_[pos+3] << 32) |
               ((uint64_t)recv_buffer_[pos+4] << 24) |
               ((uint64_t)recv_buffer_[pos+5] << 16) |
               ((uint64_t)recv_buffer_[pos+6] << 8)  |
               ((uint64_t)recv_buffer_[pos+7]);
    pos += 8;

    // Topic length (2 bytes)
    uint16_t topic_len = (recv_buffer_[pos] << 8) | recv_buffer_[pos+1];
    pos += 2;

    // Payload length (4 bytes)
    uint32_t payload_len = ((uint32_t)recv_buffer_[pos] << 24) |
                           ((uint32_t)recv_buffer_[pos+1] << 16) |
                           ((uint32_t)recv_buffer_[pos+2] << 8) |
                           ((uint32_t)recv_buffer_[pos+3]);
    pos += 4;

    // Determine if signed frame
    bool is_signed = (cmd == BinaryCommand::SIGNED_PUBLISH ||
                      cmd == BinaryCommand::SIGNED_MESSAGE);
    size_t sig_extra = is_signed ? (METRICMQ_SIGNATURE_SIZE + METRICMQ_KEY_ID_SIZE) : 0;

    // Check if we have the complete frame
    size_t total_size = BINARY_HEADER_SIZE + topic_len + payload_len + sig_extra;
    if (recv_buffer_pos_ < total_size) return false;  // Need more data

    // Parse topic
    String topic;
    if (topic_len > 0) {
        topic.reserve(topic_len);
        for (uint16_t i = 0; i < topic_len; i++) {
            topic += (char)recv_buffer_[pos + i];
        }
    }
    pos += topic_len;

    // Parse payload
    const uint8_t* payload = &recv_buffer_[pos];
    pos += payload_len;

    // Parse signature data (if signed)
    uint8_t sig_buf[METRICMQ_SIGNATURE_SIZE];
    uint32_t key_id = 0;
    if (is_signed) {
        memcpy(sig_buf, &recv_buffer_[pos], METRICMQ_SIGNATURE_SIZE);
        pos += METRICMQ_SIGNATURE_SIZE;
        key_id = ((uint32_t)recv_buffer_[pos] << 24) |
                 ((uint32_t)recv_buffer_[pos+1] << 16) |
                 ((uint32_t)recv_buffer_[pos+2] << 8) |
                 ((uint32_t)recv_buffer_[pos+3]);
        pos += METRICMQ_KEY_ID_SIZE;
    }

    // Dispatch based on command
    switch (cmd) {
        case BinaryCommand::MESSAGE:
            processMessage(topic, payload, payload_len, sequence);
            break;

        case BinaryCommand::SIGNED_MESSAGE:
            processMessage(topic, payload, payload_len, sequence, true, key_id, sig_buf);
            break;

        case BinaryCommand::PONG:
            // Keepalive response — nothing to do
            break;

        case BinaryCommand::CMD_ERROR: {
            String errMsg;
            for (uint32_t i = 0; i < payload_len; i++) errMsg += (char)payload[i];
            Serial.printf("[MetricMQ] BROKER ERROR: %s\n", errMsg.c_str());
            break;
        }

        case BinaryCommand::ACK:
            // ACK from broker (for published messages)
            break;

        default:
            break;
    }

    // Remove processed frame from buffer
    size_t remaining = recv_buffer_pos_ - total_size;
    if (remaining > 0) {
        memmove(recv_buffer_, &recv_buffer_[total_size], remaining);
    }
    recv_buffer_pos_ = remaining;
    return true;
}

void MetricMQClient::processMessage(const String& topic, const uint8_t* payload,
                                    size_t length, uint64_t sequence,
                                    bool is_signed, uint32_t key_id,
                                    const uint8_t* signature) {
    // Optional local verification of signed messages
    if (is_signed && local_verify_enabled_ && signature) {
        const VerifyKey* vk = findVerifyKey(key_id);
        if (vk) {
            // Build message: topic + payload
            size_t msg_len = topic.length() + length;
            uint8_t* msg = (uint8_t*)malloc(msg_len);
            if (msg) {
                memcpy(msg, topic.c_str(), topic.length());
                memcpy(msg + topic.length(), payload, length);
                bool valid = ed25519Verify(msg, msg_len, signature, vk->public_key);
                free(msg);
                if (!valid) {
                    Serial.printf("[MetricMQ] LOCAL VERIFY FAILED: key_id=%u topic=%s\n",
                                  key_id, topic.c_str());
                    // Still deliver — broker already verified
                }
            }
        }
    }

    Serial.printf("[MetricMQ] Received %s on '%s': %d bytes (seq=%llu%s)\n",
                  is_signed ? "SIGNED msg" : "msg",
                  topic.c_str(), length, sequence,
                  is_signed ? String(" key=" + String(key_id)).c_str() : "");

    // Invoke the appropriate callback
    if (is_signed && signed_callback_) {
        signed_callback_(topic, payload, length, sequence, true, key_id, signature);
    } else if (callback_) {
        callback_(topic, payload, length, sequence);
    }

    // ACK for exactly-once
    if (exactly_once_enabled_ && sequence > 0) {
        sendAck(sequence);
    }
    last_sequence_ = sequence;
}

const MetricMQClient::VerifyKey* MetricMQClient::findVerifyKey(uint32_t key_id) const {
    for (int i = 0; i < verify_key_count_; i++) {
        if (verify_keys_[i].valid && verify_keys_[i].key_id == key_id) {
            return &verify_keys_[i];
        }
    }
    return nullptr;
}

void MetricMQClient::resetConnection() {
    state_ = ConnectionState::DISCONNECTED;
    recv_buffer_pos_ = 0;
    last_sequence_ = 0;
}

void MetricMQClient::updateLastActivity() { last_activity_ = millis(); }

bool MetricMQClient::checkConnection() {
    unsigned long now = millis();
    if (now - last_activity_ > keep_alive_ms_ * 3) {
        Serial.println("[MetricMQ] Connection timeout");
        resetConnection();
        return false;
    }
    return true;
}

// ====================================================================
// Ed25519 sign/verify using ESP32's built-in libsodium
// Same library as the desktop broker — guaranteed wire-compatible
// ====================================================================

bool MetricMQClient::ed25519Sign(const uint8_t* message, size_t msg_len,
                                 const uint8_t* secret_key, uint8_t* signature_out) {
    // Ensure libsodium is initialized
    if (sodium_init() < 0) {
        // sodium_init returns 0 on success, 1 if already initialized, -1 on failure
        // Only -1 is a real error; 0 and 1 are fine
        Serial.println("[MetricMQ] WARNING: sodium_init returned -1");
        return false;
    }

    unsigned long long sig_len = 0;
    int rc = crypto_sign_ed25519_detached(
        signature_out, &sig_len,
        message, msg_len,
        secret_key  // 64-byte libsodium secret key (seed || public_key)
    );

    return (rc == 0 && sig_len == METRICMQ_SIGNATURE_SIZE);
}

bool MetricMQClient::ed25519Verify(const uint8_t* message, size_t msg_len,
                                   const uint8_t* signature, const uint8_t* public_key) {
    if (sodium_init() < 0) return false;

    int rc = crypto_sign_ed25519_verify_detached(
        signature,
        message, msg_len,
        public_key  // 32-byte Ed25519 public key
    );

    return (rc == 0);
}
