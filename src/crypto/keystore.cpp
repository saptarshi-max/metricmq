// Trusted Key Store Implementation
#include "metricmq/crypto.hpp"
#include <chrono>

namespace metricmq::crypto {

namespace {
    uint64_t current_timestamp() {
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()
            ).count()
        );
    }
}

uint32_t TrustedKeyStore::register_key(const PublicKey& public_key,
                                        const std::string& device_name,
                                        const std::string& allowed_topics) {
    std::unique_lock lock(mutex_);
    
    uint32_t key_id = next_key_id_++;
    
    TrustedKey tk;
    tk.public_key = public_key;
    tk.key_id = key_id;
    tk.device_name = device_name;
    tk.allowed_topics = allowed_topics;
    tk.enabled = true;
    tk.registered_at = current_timestamp();
    tk.last_used = 0;
    
    keys_[key_id] = std::move(tk);
    return key_id;
}

bool TrustedKeyStore::register_key(uint32_t key_id, const PublicKey& public_key,
                                    const std::string& device_name,
                                    const std::string& allowed_topics) {
    std::unique_lock lock(mutex_);
    
    // Check if key_id already exists
    if (keys_.contains(key_id)) {
        return false;
    }
    
    TrustedKey tk;
    tk.public_key = public_key;
    tk.key_id = key_id;
    tk.device_name = device_name;
    tk.allowed_topics = allowed_topics;
    tk.enabled = true;
    tk.registered_at = current_timestamp();
    tk.last_used = 0;
    
    keys_[key_id] = std::move(tk);
    
    // Update next_key_id if necessary
    if (key_id >= next_key_id_) {
        next_key_id_ = key_id + 1;
    }
    
    return true;
}

std::optional<PublicKey> TrustedKeyStore::get_key(uint32_t key_id) const {
    std::shared_lock lock(mutex_);
    
    auto it = keys_.find(key_id);
    if (it == keys_.end() || !it->second.enabled) {
        return std::nullopt;
    }
    return it->second.public_key;
}

std::optional<TrustedKey> TrustedKeyStore::get_key_info(uint32_t key_id) const {
    std::shared_lock lock(mutex_);
    
    auto it = keys_.find(key_id);
    if (it == keys_.end()) {
        return std::nullopt;
    }
    return it->second;
}

bool TrustedKeyStore::is_trusted(uint32_t key_id) const {
    std::shared_lock lock(mutex_);
    
    auto it = keys_.find(key_id);
    return it != keys_.end() && it->second.enabled;
}

bool TrustedKeyStore::verify_with_key(uint32_t key_id,
                                       std::span<const uint8_t> message,
                                       const Signature& signature) {
    PublicKey pk;
    {
        std::shared_lock lock(mutex_);
        auto it = keys_.find(key_id);
        if (it == keys_.end() || !it->second.enabled) {
            return false;
        }
        pk = it->second.public_key;
    }
    
    bool valid = verify(message, signature, pk);
    
    if (valid) {
        touch(key_id);
    }
    
    return valid;
}

bool TrustedKeyStore::set_enabled(uint32_t key_id, bool enabled) {
    std::unique_lock lock(mutex_);
    
    auto it = keys_.find(key_id);
    if (it == keys_.end()) {
        return false;
    }
    it->second.enabled = enabled;
    return true;
}

bool TrustedKeyStore::remove_key(uint32_t key_id) {
    std::unique_lock lock(mutex_);
    return keys_.erase(key_id) > 0;
}

std::vector<uint32_t> TrustedKeyStore::get_all_key_ids() const {
    std::shared_lock lock(mutex_);
    
    std::vector<uint32_t> ids;
    ids.reserve(keys_.size());
    for (const auto& [id, _] : keys_) {
        ids.push_back(id);
    }
    return ids;
}

size_t TrustedKeyStore::count() const {
    std::shared_lock lock(mutex_);
    return keys_.size();
}

void TrustedKeyStore::clear() {
    std::unique_lock lock(mutex_);
    keys_.clear();
    next_key_id_ = 1;
}

void TrustedKeyStore::touch(uint32_t key_id) {
    std::unique_lock lock(mutex_);
    auto it = keys_.find(key_id);
    if (it != keys_.end()) {
        it->second.last_used = current_timestamp();
    }
}

// Global singleton
TrustedKeyStore& get_global_keystore() {
    static TrustedKeyStore instance;
    return instance;
}

} // namespace metricmq::crypto
