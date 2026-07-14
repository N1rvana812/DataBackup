#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>

namespace backup {

// ============================================================================
// Streaming AES-256-CTR Encryptor / Decryptor
// ============================================================================
//
// Uses OpenSSL's EVP interface for AES-256 in CTR mode.
// The CTR counter state is maintained across multiple encrypt/decrypt calls,
// providing a continuous stream within a single file or entire archive.
//
// CTR mode: encryption and decryption are the same operation (XOR with
// keystream), so encrypt() and decrypt() both call the same internal function.
// ============================================================================

class StreamEncryptor {
public:
    StreamEncryptor();
    ~StreamEncryptor();

    StreamEncryptor(const StreamEncryptor&) = delete;
    StreamEncryptor& operator=(const StreamEncryptor&) = delete;
    StreamEncryptor(StreamEncryptor&&) noexcept;
    StreamEncryptor& operator=(StreamEncryptor&&) noexcept;

    // Initialize the encryptor with a key and IV.
    // Must be called before any encrypt/decrypt operations.
    // @param key     32-byte AES-256 key
    // @param keySize Must be 32 (AES-256)
    // @param iv      16-byte initialization vector (CTR counter start)
    // @param ivSize  Must be 16
    // @return        true on success
    bool init(const uint8_t* key, size_t keySize,
              const uint8_t* iv, size_t ivSize);

    // Encrypt data. CTR mode: this XORs the plaintext with the keystream.
    // @param data   Plaintext data
    // @param size   Data size in bytes
    // @return       Ciphertext (same size as input)
    std::vector<uint8_t> encrypt(const uint8_t* data, size_t size);

    // Decrypt data. CTR mode: identical operation to encrypt.
    // @param data   Ciphertext data
    // @param size   Data size in bytes
    // @return       Plaintext (same size as input)
    std::vector<uint8_t> decrypt(const uint8_t* data, size_t size);

    // Check if the encryptor has been successfully initialized
    bool isInitialized() const;

private:
    // Internal context pointer (opaque OpenSSL EVP_CIPHER_CTX)
    void* ctx_ = nullptr;
    bool initialized_ = false;

    // Internal process function (same for encrypt and decrypt in CTR mode)
    std::vector<uint8_t> process(const uint8_t* data, size_t size);
};

} // namespace backup
