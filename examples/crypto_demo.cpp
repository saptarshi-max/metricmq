// Example: Ed25519 signing demo for MetricMQ
#include "metricmq/crypto.hpp"
#include <iostream>
#include <string>

int main() {
    std::cout << "=== MetricMQ Ed25519 Signing Demo ===\n\n";

    // Initialize libsodium
    if (!metricmq::crypto::init()) {
        std::cerr << "Failed to initialize crypto\n";
        return 1;
    }
    std::cout << "[OK] Crypto initialized\n";

    // Generate a key pair (simulating device registration)
    auto keypair = metricmq::crypto::generate_keypair();
    std::cout << "[OK] Generated key pair\n";
    std::cout << "    Public key: " << metricmq::crypto::to_hex(keypair.public_key).substr(0, 16) << "...\n";

    // Simulate a message to publish
    std::string topic = "sensors/temperature";
    std::string payload = R"({"device":"esp32-001","temp":23.5,"humidity":65})";
    std::string message = topic + ":" + payload;

    // Sign the message
    auto signature = metricmq::crypto::sign(message, keypair.secret_key);
    std::cout << "\n[SIGN] Message: " << message << "\n";
    std::cout << "       Signature: " << metricmq::crypto::to_hex(signature).substr(0, 32) << "...\n";

    // Verify the signature (broker side)
    bool valid = metricmq::crypto::verify(message, signature, keypair.public_key);
    std::cout << "\n[VERIFY] Result: " << (valid ? "VALID" : "INVALID") << "\n";

    // Test tampering detection
    std::string tampered = topic + ":" + R"({"device":"esp32-001","temp":99.9,"humidity":65})";
    bool tampered_valid = metricmq::crypto::verify(tampered, signature, keypair.public_key);
    std::cout << "\n[TAMPER TEST] Modified payload验证: " << (tampered_valid ? "VALID (BAD!)" : "INVALID (GOOD!)") << "\n";

    // Clean up secret key
    metricmq::crypto::secure_zero(keypair.secret_key);
    std::cout << "\n[OK] Secret key securely wiped\n";

    std::cout << "\n=== Demo Complete ===\n";
    return 0;
}
