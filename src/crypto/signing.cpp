// Ed25519 Signing/Verification Implementation
#include "signing.hpp"
#include <sodium.h>
#include <atomic>
#include <sstream>
#include <iomanip>

namespace metricmq::crypto {

namespace {
    std::atomic<bool> g_initialized{false};
}

bool init() {
    if (g_initialized.load(std::memory_order_acquire)) {
        return true;
    }
    
    if (sodium_init() < 0) {
        return false;
    }
    
    g_initialized.store(true, std::memory_order_release);
    return true;
}

bool is_initialized() {
    return g_initialized.load(std::memory_order_acquire);
}

KeyPair generate_keypair() {
    KeyPair kp;
    crypto_sign_keypair(kp.public_key.data(), kp.secret_key.data());
    return kp;
}

KeyPair generate_keypair_from_seed(const Seed& seed) {
    KeyPair kp;
    crypto_sign_seed_keypair(kp.public_key.data(), kp.secret_key.data(), seed.data());
    return kp;
}

Signature sign(std::span<const uint8_t> message, const SecretKey& secret_key) {
    Signature sig;
    crypto_sign_detached(
        sig.data(), 
        nullptr, 
        message.data(), 
        message.size(), 
        secret_key.data()
    );
    return sig;
}

Signature sign(std::string_view message, const SecretKey& secret_key) {
    return sign(
        std::span<const uint8_t>(
            reinterpret_cast<const uint8_t*>(message.data()), 
            message.size()
        ), 
        secret_key
    );
}

bool verify(std::span<const uint8_t> message, 
            const Signature& signature, 
            const PublicKey& public_key) {
    return crypto_sign_verify_detached(
        signature.data(), 
        message.data(), 
        message.size(), 
        public_key.data()
    ) == 0;
}

bool verify(std::string_view message, 
            const Signature& signature, 
            const PublicKey& public_key) {
    return verify(
        std::span<const uint8_t>(
            reinterpret_cast<const uint8_t*>(message.data()), 
            message.size()
        ), 
        signature,
        public_key
    );
}

std::string to_hex(const PublicKey& key) {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (uint8_t byte : key) {
        oss << std::setw(2) << static_cast<int>(byte);
    }
    return oss.str();
}

std::string to_hex(const Signature& sig) {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (uint8_t byte : sig) {
        oss << std::setw(2) << static_cast<int>(byte);
    }
    return oss.str();
}

namespace {
    std::optional<uint8_t> hex_char_to_nibble(char c) {
        if (c >= '0' && c <= '9') return static_cast<uint8_t>(c - '0');
        if (c >= 'a' && c <= 'f') return static_cast<uint8_t>(c - 'a' + 10);
        if (c >= 'A' && c <= 'F') return static_cast<uint8_t>(c - 'A' + 10);
        return std::nullopt;
    }
}

std::optional<PublicKey> public_key_from_hex(std::string_view hex) {
    if (hex.size() != PUBLIC_KEY_SIZE * 2) {
        return std::nullopt;
    }
    
    PublicKey key;
    for (size_t i = 0; i < PUBLIC_KEY_SIZE; ++i) {
        auto high = hex_char_to_nibble(hex[i * 2]);
        auto low = hex_char_to_nibble(hex[i * 2 + 1]);
        if (!high || !low) {
            return std::nullopt;
        }
        key[i] = static_cast<uint8_t>((*high << 4) | *low);
    }
    return key;
}

std::optional<Signature> signature_from_hex(std::string_view hex) {
    if (hex.size() != SIGNATURE_SIZE * 2) {
        return std::nullopt;
    }
    
    Signature sig;
    for (size_t i = 0; i < SIGNATURE_SIZE; ++i) {
        auto high = hex_char_to_nibble(hex[i * 2]);
        auto low = hex_char_to_nibble(hex[i * 2 + 1]);
        if (!high || !low) {
            return std::nullopt;
        }
        sig[i] = static_cast<uint8_t>((*high << 4) | *low);
    }
    return sig;
}

void secure_zero(void* ptr, size_t size) {
    sodium_memzero(ptr, size);
}

} // namespace metricmq::crypto
