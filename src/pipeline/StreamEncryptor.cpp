#include "pipeline/StreamEncryptor.h"

#ifdef HAS_OPENSSL
#include <openssl/evp.h>
#endif

#include <cstring>
#include <stdexcept>

namespace backup {

StreamEncryptor::StreamEncryptor() = default;

StreamEncryptor::~StreamEncryptor() {
#ifdef HAS_OPENSSL
    if (ctx_ != nullptr) {
        EVP_CIPHER_CTX_free(static_cast<EVP_CIPHER_CTX*>(ctx_));
        ctx_ = nullptr;
    }
#endif
}

StreamEncryptor::StreamEncryptor(StreamEncryptor&& other) noexcept
    : ctx_(other.ctx_)
    , initialized_(other.initialized_) {
    other.ctx_ = nullptr;
    other.initialized_ = false;
}

StreamEncryptor& StreamEncryptor::operator=(StreamEncryptor&& other) noexcept {
    if (this != &other) {
#ifdef HAS_OPENSSL
        if (ctx_ != nullptr) {
            EVP_CIPHER_CTX_free(static_cast<EVP_CIPHER_CTX*>(ctx_));
        }
#endif
        ctx_ = other.ctx_;
        initialized_ = other.initialized_;
        other.ctx_ = nullptr;
        other.initialized_ = false;
    }
    return *this;
}

bool StreamEncryptor::init(const uint8_t* key, size_t keySize,
                            const uint8_t* iv, size_t ivSize) {
#ifdef HAS_OPENSSL
    if (key == nullptr || iv == nullptr) {
        return false;
    }
    if (keySize != 32) {
        return false;
    }
    if (ivSize != 16) {
        return false;
    }

    if (ctx_ != nullptr) {
        EVP_CIPHER_CTX_free(static_cast<EVP_CIPHER_CTX*>(ctx_));
        ctx_ = nullptr;
    }
    initialized_ = false;

    EVP_CIPHER_CTX* context = EVP_CIPHER_CTX_new();
    if (context == nullptr) {
        return false;
    }

    int rc = EVP_EncryptInit_ex(context, EVP_aes_256_ctr(), nullptr, key, iv);
    if (rc != 1) {
        EVP_CIPHER_CTX_free(context);
        return false;
    }

    ctx_ = context;
    initialized_ = true;
    return true;
#else
    (void)key;
    (void)keySize;
    (void)iv;
    (void)ivSize;
    return false;
#endif
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
#ifdef HAS_OPENSSL
    if (!initialized_) {
        throw std::runtime_error("StreamEncryptor: not initialized");
    }
    if (data == nullptr || size == 0) {
        return {};
    }

    EVP_CIPHER_CTX* context = static_cast<EVP_CIPHER_CTX*>(ctx_);

    std::vector<uint8_t> output(size + EVP_CIPHER_CTX_block_size(context));

    int outLen = 0;
    int rc = EVP_EncryptUpdate(context, output.data(), &outLen, data, static_cast<int>(size));
    if (rc != 1) {
        throw std::runtime_error("StreamEncryptor: EVP_EncryptUpdate failed");
    }

    output.resize(static_cast<size_t>(outLen));
    return output;
#else
    (void)data;
    (void)size;
    throw std::runtime_error("StreamEncryptor: OpenSSL support not compiled in");
#endif
}

} // namespace backup
