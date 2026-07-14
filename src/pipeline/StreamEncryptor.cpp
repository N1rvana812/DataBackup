#include "pipeline/StreamEncryptor.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace backup {

// ============================================================================
// RC4 Stream Cipher Implementation
// ============================================================================
//
// RC4 (Rivest Cipher 4 / "ARC4") is a simple, widely-understood stream cipher.
// It operates in two phases:
//
// 1. KSA (Key Scheduling Algorithm):
//    - Initializes a 256-byte S-box as the identity permutation S[i] = i
//    - Shuffles S based on the key bytes using a simple swap loop
//
// 2. PRGA (Pseudo-Random Generation Algorithm):
//    - Produces a pseudo-random keystream byte by byte
//    - Each keystream byte is XORed with the corresponding plaintext byte
//
// Encryption and decryption are identical: XOR the data with the keystream.
// The S-box state persists across calls, allowing streaming operation.
// ============================================================================

namespace {

// Swap two bytes in the S-box
inline void swap(uint8_t& a, uint8_t& b) {
    uint8_t t = a;
    a = b;
    b = t;
}

}  // namespace

StreamEncryptor::StreamEncryptor() {
    // Initialize S-box to identity permutation
    for (int k = 0; k < 256; ++k) {
        S_[k] = static_cast<uint8_t>(k);
    }
}

StreamEncryptor::~StreamEncryptor() {
    // Securely clear the S-box
    std::memset(S_, 0, sizeof(S_));
    i_ = 0;
    j_ = 0;
    initialized_ = false;
}

StreamEncryptor::StreamEncryptor(StreamEncryptor&& other) noexcept
    : i_(other.i_)
    , j_(other.j_)
    , initialized_(other.initialized_) {
    std::memcpy(S_, other.S_, sizeof(S_));
    // Clear the moved-from object
    std::memset(other.S_, 0, sizeof(other.S_));
    other.i_ = 0;
    other.j_ = 0;
    other.initialized_ = false;
}

StreamEncryptor& StreamEncryptor::operator=(StreamEncryptor&& other) noexcept {
    if (this != &other) {
        std::memcpy(S_, other.S_, sizeof(S_));
        i_ = other.i_;
        j_ = other.j_;
        initialized_ = other.initialized_;
        // Clear the moved-from object
        std::memset(other.S_, 0, sizeof(other.S_));
        other.i_ = 0;
        other.j_ = 0;
        other.initialized_ = false;
    }
    return *this;
}

bool StreamEncryptor::init(const uint8_t* key, size_t keySize,
                            const uint8_t* iv, size_t ivSize) {
    if (key == nullptr || iv == nullptr) {
        return false;
    }
    if (keySize == 0 || ivSize == 0) {
        return false;
    }

    // Keep backward compatibility: keySize must be 32, ivSize must be 16
    // (This matches the existing AES-256-CTR API contract)
    if (keySize != 32 || ivSize != 16) {
        return false;
    }

    // Build the combined key material: key || iv
    std::vector<uint8_t> combined(keySize + ivSize);
    std::memcpy(combined.data(), key, keySize);
    std::memcpy(combined.data() + keySize, iv, ivSize);

    // --- Key Scheduling Algorithm (KSA) ---
    // Initialize S-box to identity permutation
    for (int k = 0; k < 256; ++k) {
        S_[k] = static_cast<uint8_t>(k);
    }

    uint8_t j = 0;
    const size_t combinedSize = combined.size();
    for (int k = 0; k < 256; ++k) {
        j = static_cast<uint8_t>(j + S_[k] + combined[k % combinedSize]);
        swap(S_[k], S_[j]);
    }

    // Reset PRGA indices
    i_ = 0;
    j_ = 0;
    initialized_ = true;

    // Discard the first 256 bytes of keystream (RC4-drop256)
    // to mitigate known weaknesses in the initial keystream output
    for (int k = 0; k < 256; ++k) {
        i_ = static_cast<uint8_t>(i_ + 1);
        j_ = static_cast<uint8_t>(j_ + S_[i_]);
        swap(S_[i_], S_[j_]);
        // Discard: S_[(S_[i_] + S_[j_]) & 0xFF]
        (void)S_[(S_[i_] + S_[j_]) & 0xFF];
    }

    return true;
}

std::vector<uint8_t> StreamEncryptor::encrypt(const uint8_t* data, size_t size) {
    return process(data, size);
}

std::vector<uint8_t> StreamEncryptor::decrypt(const uint8_t* data, size_t size) {
    return process(data, size);
}

bool StreamEncryptor::isInitialized() const {
    return initialized_;
}

std::vector<uint8_t> StreamEncryptor::process(const uint8_t* data, size_t size) {
    if (!initialized_) {
        throw std::runtime_error("StreamEncryptor: not initialized");
    }
    if (data == nullptr || size == 0) {
        return {};
    }

    std::vector<uint8_t> output(size);

    for (size_t k = 0; k < size; ++k) {
        // PRGA: generate next keystream byte
        i_ = static_cast<uint8_t>(i_ + 1);
        j_ = static_cast<uint8_t>(j_ + S_[i_]);
        swap(S_[i_], S_[j_]);
        const uint8_t keystream = S_[(S_[i_] + S_[j_]) & 0xFF];

        // XOR plaintext with keystream
        output[k] = data[k] ^ keystream;
    }

    return output;
}

} // namespace backup
