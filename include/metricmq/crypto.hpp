// MetricMQ Crypto API - Ed25519 Signing
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <shared_mutex>
#include <vector>

namespace metricmq::crypto {

// Ed25519 sizes
inline constexpr size_t PUBLIC_KEY_SIZE = 32;
inline constexpr size_t SECRET_KEY_SIZE = 64;
inline constexpr size_t SIGNATURE_SIZE = 64;
inline constexpr size_t SEED_SIZE = 32;

using PublicKey = std::array<uint8_t, PUBLIC_KEY_SIZE>;
using SecretKey = std::array<uint8_t, SECRET_KEY_SIZE>;
using Signature = std::array<uint8_t, SIGNATURE_SIZE>;
using Seed = std::array<uint8_t, SEED_SIZE>;

struct KeyPair {
    PublicKey public_key;
    SecretKey secret_key;
    uint32_t key_id{0};
};

// Initialize crypto subsystem (call once)
[[nodiscard]] bool init();
[[nodiscard]] bool is_initialized();

// Key generation
[[nodiscard]] KeyPair generate_keypair();
[[nodiscard]] KeyPair generate_keypair_from_seed(const Seed& seed);

// Sign/verify
[[nodiscard]] Signature sign(std::span<const uint8_t> message, const SecretKey& secret_key);
[[nodiscard]] Signature sign(std::string_view message, const SecretKey& secret_key);
[[nodiscard]] bool verify(std::span<const uint8_t> message, const Signature& signature, const PublicKey& public_key);
[[nodiscard]] bool verify(std::string_view message, const Signature& signature, const PublicKey& public_key);

// Hex encoding
[[nodiscard]] std::string to_hex(const PublicKey& key);
[[nodiscard]] std::string to_hex(const Signature& sig);
[[nodiscard]] std::optional<PublicKey> public_key_from_hex(std::string_view hex);
[[nodiscard]] std::optional<Signature> signature_from_hex(std::string_view hex);

// Secure memory
void secure_zero(void* ptr, size_t size);

template<typename T>
void secure_zero(T& container) {
    secure_zero(container.data(), container.size());
}

// Trusted key metadata
struct TrustedKey {
    PublicKey public_key;
    uint32_t key_id;
    std::string device_name;
    std::string allowed_topics;
    bool enabled{true};
    uint64_t registered_at{0};
    uint64_t last_used{0};
};

// Thread-safe trusted key store for broker
class TrustedKeyStore {
public:
    TrustedKeyStore() = default;
    ~TrustedKeyStore() = default;
    
    TrustedKeyStore(const TrustedKeyStore&) = delete;
    TrustedKeyStore& operator=(const TrustedKeyStore&) = delete;
    TrustedKeyStore(TrustedKeyStore&&) = default;
    TrustedKeyStore& operator=(TrustedKeyStore&&) = default;

    uint32_t register_key(const PublicKey& public_key,
                          const std::string& device_name = "",
                          const std::string& allowed_topics = "");
    bool register_key(uint32_t key_id, const PublicKey& public_key,
                      const std::string& device_name = "",
                      const std::string& allowed_topics = "");
    std::optional<PublicKey> get_key(uint32_t key_id) const;
    std::optional<TrustedKey> get_key_info(uint32_t key_id) const;
    bool is_trusted(uint32_t key_id) const;
    bool verify_with_key(uint32_t key_id, std::span<const uint8_t> message, const Signature& signature);
    bool set_enabled(uint32_t key_id, bool enabled);
    bool remove_key(uint32_t key_id);
    std::vector<uint32_t> get_all_key_ids() const;
    size_t count() const;
    void clear();
    void touch(uint32_t key_id);

private:
    mutable std::shared_mutex mutex_;
    std::unordered_map<uint32_t, TrustedKey> keys_;
    uint32_t next_key_id_{1};
};

// Global keystore singleton
TrustedKeyStore& get_global_keystore();

} // namespace metricmq::crypto
