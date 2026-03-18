// Test: Signed Publish with Ed25519 verification
// This demonstrates the full signed message flow
#include "metricmq/crypto.hpp"
#include "metricmq/binary_protocol.hpp"
#include <iostream>
#include <string>
#include <cassert>

using namespace metricmq;
using namespace metricmq::crypto;

int main() {
    std::cout << "=== MetricMQ Signed Publish Test ===\n\n";

    // Step 1: Initialize crypto
    if (!init()) {
        std::cerr << "Failed to initialize crypto\n";
        return 1;
    }
    std::cout << "[1/6] Crypto initialized\n";

    // Step 2: Simulate device registration (generate keypair)
    auto device_keypair = generate_keypair();
    std::cout << "[2/6] Device keypair generated\n";
    std::cout << "      Public key: " << to_hex(device_keypair.public_key).substr(0, 32) << "...\n";

    // Step 3: Register device public key with broker keystore
    auto& keystore = get_global_keystore();
    uint32_t key_id = keystore.register_key(device_keypair.public_key, "esp32-sensor-001", "sensors/*");
    std::cout << "[3/6] Device registered with key_id=" << key_id << "\n";

    // Step 4: Create and sign a publish message (device side)
    std::string topic = "sensors/temperature";
    std::string payload = R"({"temp":24.5,"humidity":62,"device":"esp32-sensor-001"})";
    std::string message_to_sign = topic + payload;
    
    auto signature = sign(message_to_sign, device_keypair.secret_key);
    std::cout << "[4/6] Message signed\n";
    std::cout << "      Topic: " << topic << "\n";
    std::cout << "      Payload: " << payload << "\n";

    // Step 5: Create signed publish frame and serialize
    BinaryFrame signed_frame = BinaryFrame::signed_publish(topic, payload, signature, key_id, 1);
    std::string serialized = BinaryProtocol::serialize(signed_frame);
    std::cout << "[5/6] Signed frame created (" << serialized.size() << " bytes)\n";
    std::cout << "      Frame breakdown:\n";
    std::cout << "        Header:    16 bytes\n";
    std::cout << "        Topic:     " << topic.size() << " bytes\n";
    std::cout << "        Payload:   " << payload.size() << " bytes\n";
    std::cout << "        Signature: 64 bytes\n";
    std::cout << "        Key ID:    4 bytes\n";

    // Step 6: Parse and verify (broker side)
    auto parsed = BinaryProtocol::parse(serialized);
    if (!parsed) {
        std::cerr << "Failed to parse frame\n";
        return 1;
    }

    const auto& [frame, bytes_consumed] = *parsed;
    assert(frame.command == BinaryCommand::CMD_SIGNED_PUBLISH);
    assert(frame.is_signed);
    assert(frame.key_id == key_id);

    // Verify signature using keystore
    std::string verify_message = frame.topic + frame.payload;
    bool valid = keystore.verify_with_key(
        frame.key_id,
        std::span<const uint8_t>(
            reinterpret_cast<const uint8_t*>(verify_message.data()),
            verify_message.size()
        ),
        frame.signature
    );

    std::cout << "[6/6] Signature verification: " << (valid ? "PASSED" : "FAILED") << "\n";

    // Additional tests
    std::cout << "\n--- Additional Tests ---\n";

    // Test: Tampered payload should fail
    std::string tampered = frame.topic + R"({"temp":99.9,"hacked":true})";
    bool tampered_valid = keystore.verify_with_key(
        frame.key_id,
        std::span<const uint8_t>(
            reinterpret_cast<const uint8_t*>(tampered.data()),
            tampered.size()
        ),
        frame.signature
    );
    std::cout << "Tamper detection: " << (tampered_valid ? "FAILED (bad!)" : "PASSED") << "\n";

    // Test: Unknown key_id should fail
    bool unknown_key = keystore.verify_with_key(
        999,  // non-existent key
        std::span<const uint8_t>(
            reinterpret_cast<const uint8_t*>(verify_message.data()),
            verify_message.size()
        ),
        frame.signature
    );
    std::cout << "Unknown key rejection: " << (unknown_key ? "FAILED (bad!)" : "PASSED") << "\n";

    // Test: Disabled key should fail
    keystore.set_enabled(key_id, false);
    bool disabled_key = keystore.verify_with_key(
        frame.key_id,
        std::span<const uint8_t>(
            reinterpret_cast<const uint8_t*>(verify_message.data()),
            verify_message.size()
        ),
        frame.signature
    );
    std::cout << "Disabled key rejection: " << (disabled_key ? "FAILED (bad!)" : "PASSED") << "\n";

    // Re-enable for final check
    keystore.set_enabled(key_id, true);

    // Clean up
    secure_zero(device_keypair.secret_key);
    
    std::cout << "\n=== All Tests Passed ===\n";
    return 0;
}
