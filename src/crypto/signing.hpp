// Ed25519 Signing/Verification using libsodium
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace metricmq::crypto {

// Ed25519 key sizes
inline constexpr size_t PUBLIC_KEY_SIZE = 32;   // crypto_sign_PUBLICKEYBYTES
inline constexpr size_t SECRET_KEY_SIZE = 64;   // crypto_sign_SECRETKEYBYTES
inline constexpr size_t SIGNATURE_SIZE = 64;    // crypto_sign_BYTES
inline constexpr size_t SEED_SIZE = 32;         // crypto_sign_SEEDBYTES

// Type aliases for clarity
using PublicKey = std::array<uint8_t, PUBLIC_KEY_SIZE>;
using SecretKey = std::array<uint8_t, SECRET_KEY_SIZE>;
using Signature = std::array<uint8_t, SIGNATURE_SIZE>;
using Seed = std::array<uint8_t, SEED_SIZE>;

// Key pair structure
struct KeyPair {
    PublicKey public_key;
    SecretKey secret_key;
    uint32_t key_id{0};  // Optional identifier for key lookup
};

// Initialize libsodium (call once at startup)
// Returns false if initialization fails
[[nodiscard]] bool init();

// Check if libsodium is initialized
[[nodiscard]] bool is_initialized();

// Generate a new Ed25519 key pair
[[nodiscard]] KeyPair generate_keypair();

// Generate key pair from seed (deterministic)
[[nodiscard]] KeyPair generate_keypair_from_seed(const Seed& seed);

// Sign a message
// Returns 64-byte signature
[[nodiscard]] Signature sign(std::span<const uint8_t> message, const SecretKey& secret_key);

// Sign overload for string_view
[[nodiscard]] Signature sign(std::string_view message, const SecretKey& secret_key);

// Verify a signature
// Returns true if signature is valid
[[nodiscard]] bool verify(std::span<const uint8_t> message, 
                          const Signature& signature, 
                          const PublicKey& public_key);

// Verify overload for string_view
[[nodiscard]] bool verify(std::string_view message, 
                          const Signature& signature, 
                          const PublicKey& public_key);

// Utility: Convert key to hex string
[[nodiscard]] std::string to_hex(const PublicKey& key);
[[nodiscard]] std::string to_hex(const Signature& sig);

// Utility: Parse hex string to key
[[nodiscard]] std::optional<PublicKey> public_key_from_hex(std::string_view hex);
[[nodiscard]] std::optional<Signature> signature_from_hex(std::string_view hex);

// Utility: Secure memory zeroing
void secure_zero(void* ptr, size_t size);

template<typename T>
void secure_zero(T& container) {
    secure_zero(container.data(), container.size());
}

} // namespace metricmq::crypto
