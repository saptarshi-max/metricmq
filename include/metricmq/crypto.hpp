/**
 * @file crypto.hpp
 * @brief Ed25519 message signing and the broker-side trusted key registry.
 *
 * Provides free functions for key generation, signing, verification, and secure
 * memory erasure (all backed by libsodium), plus the thread-safe `TrustedKeyStore`
 * that the broker uses to verify signed publish frames.
 *
 * @par One-time initialization
 * Call `init()` once at startup. `main.cpp` does this for the broker binary.
 *
 * @par Signing convention
 * The signed region is `topic + payload` (raw concatenation, no separator).
 * Both `session.cpp` and the ESP32 client library use this format.
 *
 * @note All sign/verify functions are `[[nodiscard]]`.
 */
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

/// @name Size constants (bytes)
/// @{
inline constexpr size_t PUBLIC_KEY_SIZE = 32; ///< Ed25519 public key
inline constexpr size_t SECRET_KEY_SIZE = 64; ///< Ed25519 secret key (libsodium expanded)
inline constexpr size_t SIGNATURE_SIZE  = 64; ///< Ed25519 signature
inline constexpr size_t SEED_SIZE       = 32; ///< Deterministic seed
/// @}

using PublicKey = std::array<uint8_t, PUBLIC_KEY_SIZE>; ///< 32-byte public key
using SecretKey = std::array<uint8_t, SECRET_KEY_SIZE>; ///< 64-byte secret key — keep private
using Signature = std::array<uint8_t, SIGNATURE_SIZE>;  ///< 64-byte Ed25519 signature
using Seed      = std::array<uint8_t, SEED_SIZE>;        ///< 32-byte deterministic seed

/**
 * @brief Holds both halves of an Ed25519 keypair.
 *
 * Distribute `public_key` to the broker. Embed `secret_key` in device firmware.
 * Call `secure_zero(secret_key)` everywhere it must not linger.
 */
struct KeyPair {
    PublicKey public_key; ///< 32-byte public key — safe to share
    SecretKey secret_key; ///< 64-byte secret key — keep private, erase after use
    uint32_t  key_id{0};  ///< Assigned by TrustedKeyStore::register_key() (0 until registered)
};

/// @name Initialization
/// @{
/** @brief Init libsodium. Idempotent. @return `true` on success. */
[[nodiscard]] bool init();
/** @brief Returns `true` if init() has been called successfully. */
[[nodiscard]] bool is_initialized();
/// @}

/// @name Key Generation
/// @{
/** @brief Generate a random Ed25519 keypair. @pre `init()` must have been called. */
[[nodiscard]] KeyPair generate_keypair();
/**
 * @brief Derive a deterministic keypair from a 32-byte seed.
 *
 * Useful for devices that store a seed in flash instead of a full 64-byte key.
 * The same seed always produces the same keypair.
 */
[[nodiscard]] KeyPair generate_keypair_from_seed(const Seed& seed);
/// @}

/// @name Sign / Verify
/// @{
/** @brief Sign a byte span. @return 64-byte signature. */
[[nodiscard]] Signature sign(std::span<const uint8_t> message, const SecretKey& secret_key);
/** @brief Sign a string (treated as raw bytes). @return 64-byte signature. */
[[nodiscard]] Signature sign(std::string_view message, const SecretKey& secret_key);
/** @brief Verify a signature against a byte span. @return `true` if valid. */
[[nodiscard]] bool verify(std::span<const uint8_t> message, const Signature& signature, const PublicKey& public_key);
/** @brief Verify a signature against a string. @return `true` if valid. */
[[nodiscard]] bool verify(std::string_view message, const Signature& signature, const PublicKey& public_key);
/// @}

/// @name Hex Utilities
/// @{
/** @brief Encode a public key as a 64-char lowercase hex string. */
[[nodiscard]] std::string to_hex(const PublicKey& key);
/** @brief Encode a signature as a 128-char lowercase hex string. */
[[nodiscard]] std::string to_hex(const Signature& sig);
/** @brief Decode a 64-char hex string to a public key. Returns nullopt on error. */
[[nodiscard]] std::optional<PublicKey> public_key_from_hex(std::string_view hex);
/** @brief Decode a 128-char hex string to a signature. Returns nullopt on error. */
[[nodiscard]] std::optional<Signature> signature_from_hex(std::string_view hex);
/// @}

/// @name Secure Memory
/// @{
/**
 * @brief Overwrite memory with zeros, preventing compiler elision.
 *
 * Uses `sodium_memzero` internally. Call on `SecretKey` arrays before destruction
 * or whenever the key leaves scope.
 */
void secure_zero(void* ptr, size_t size);

/** @brief Overwrite any contiguous container (array, string, vector) with zeros. */
template<typename T>
void secure_zero(T& container) {
    secure_zero(container.data(), container.size());
}
/// @}

/**
 * @brief Metadata record for one entry in TrustedKeyStore.
 */
struct TrustedKey {
    PublicKey   public_key;       ///< 32-byte Ed25519 public key
    uint32_t    key_id;           ///< Numeric identifier
    std::string device_name;      ///< Human-readable label (for logs)
    std::string allowed_topics;   ///< Topic scope: `"*"` = all, `"prefix/*"` = prefix, exact otherwise
    bool        enabled{true};    ///< false = revoked (not deleted, kept for audit)
    uint64_t    registered_at{0}; ///< Unix timestamp of registration
    uint64_t    last_used{0};     ///< Unix timestamp of last successful verify_with_key()
};

/**
 * @brief Thread-safe registry of trusted Ed25519 public keys.
 *
 * The broker's session layer calls `verify_with_key()` on every CMD_SIGNED_PUBLISH.
 * Use `get_global_keystore()` to access the process-wide singleton.
 *
 * @par Thread safety
 * Reads use a shared lock; writes use an exclusive lock. Safe from multiple threads.
 */
class TrustedKeyStore {
public:
    TrustedKeyStore() = default;
    ~TrustedKeyStore() = default;

    TrustedKeyStore(const TrustedKeyStore&) = delete;
    TrustedKeyStore& operator=(const TrustedKeyStore&) = delete;
    TrustedKeyStore(TrustedKeyStore&&) = default;
    TrustedKeyStore& operator=(TrustedKeyStore&&) = default;

    /**
     * @brief Register a key with an auto-assigned ID.
     * @return The assigned key_id (starts at 1, increments per call).
     */
    uint32_t register_key(const PublicKey& public_key,
                          const std::string& device_name = "",
                          const std::string& allowed_topics = "");

    /**
     * @brief Register a key with a caller-specified ID.
     * @return `true` on success, `false` if the ID is already in use.
     */
    bool register_key(uint32_t key_id, const PublicKey& public_key,
                      const std::string& device_name = "",
                      const std::string& allowed_topics = "");

    /** @brief Look up a public key by ID. Returns nullopt if not found or disabled. */
    std::optional<PublicKey> get_key(uint32_t key_id) const;

    /** @brief Look up full metadata. Returns nullopt if key_id not found. */
    std::optional<TrustedKey> get_key_info(uint32_t key_id) const;

    /** @brief Returns `true` if the key exists and is enabled. */
    bool is_trusted(uint32_t key_id) const;

    /**
     * @brief Verify a message signature against a registered public key.
     *
     * Checks existence, enabled state, and Ed25519 signature validity.
     * Updates `last_used` timestamp on success.
     *
     * @return `true` if the signature is valid and the key is active.
     */
    bool verify_with_key(uint32_t key_id, std::span<const uint8_t> message, const Signature& signature);

    /**
     * @brief Enable or disable a key (for revocation without deletion).
     * @return `true` if the key was found and updated.
     */
    bool set_enabled(uint32_t key_id, bool enabled);

    /** @brief Permanently remove a key. @return `true` if key existed. */
    bool remove_key(uint32_t key_id);

    /** @brief Return all registered key IDs. */
    std::vector<uint32_t> get_all_key_ids() const;

    /** @brief Return the total number of registered keys. */
    size_t count() const;

    /** @brief Remove all keys. */
    void clear();

    /** @brief Update the `last_used` timestamp (called internally after verification). */
    void touch(uint32_t key_id);

private:
    mutable std::shared_mutex mutex_;
    std::unordered_map<uint32_t, TrustedKey> keys_;
    uint32_t next_key_id_{1};
};

/**
 * @brief Access the process-wide global TrustedKeyStore singleton.
 *
 * Register device keys here at broker startup.
 * The session layer calls `verify_with_key()` on it for every signed frame.
 */
TrustedKeyStore& get_global_keystore();

} // namespace metricmq::crypto
