#include "pipeline/KeyDerivation.h"

#ifdef HAS_OPENSSL
#include <openssl/evp.h>
#include <openssl/rand.h>
#endif

#include <cstring>
#include <fstream>
#include <stdexcept>

namespace backup {

std::vector<uint8_t> KeyDerivation::deriveKey(const std::string& password,
                                                const uint8_t* salt,
                                                size_t saltSize,
                                                size_t keySize,
                                                unsigned int iterations) {
#ifdef HAS_OPENSSL
    if (password.empty()) {
        throw std::invalid_argument("KeyDerivation: password must not be empty");
    }
    if (salt == nullptr || saltSize == 0) {
        throw std::invalid_argument("KeyDerivation: salt must be provided");
    }

    std::vector<uint8_t> key(keySize);

    int rc = PKCS5_PBKDF2_HMAC(
        password.data(),
        static_cast<int>(password.size()),
        salt,
        static_cast<int>(saltSize),
        static_cast<int>(iterations),
        EVP_sha256(),
        static_cast<int>(keySize),
        key.data());

    if (rc != 1) {
        throw std::runtime_error("KeyDerivation: PKCS5_PBKDF2_HMAC failed");
    }

    return key;
#else
    (void)password;
    (void)salt;
    (void)saltSize;
    (void)keySize;
    (void)iterations;
    throw std::runtime_error("KeyDerivation: OpenSSL support not compiled in");
#endif
}

std::vector<uint8_t> KeyDerivation::generateRandomBytes(size_t count) {
#ifdef HAS_OPENSSL
    std::vector<uint8_t> buffer(count);
    int rc = RAND_bytes(buffer.data(), static_cast<int>(count));
    if (rc != 1) {
        throw std::runtime_error("KeyDerivation: RAND_bytes failed to generate random data");
    }
    return buffer;
#else
    // Fallback: use /dev/urandom
    std::vector<uint8_t> buffer(count);
    std::ifstream urandom("/dev/urandom", std::ios::binary);
    if (!urandom.read(reinterpret_cast<char*>(buffer.data()),
                       static_cast<std::streamsize>(count))) {
        throw std::runtime_error("KeyDerivation: failed to read from /dev/urandom");
    }
    return buffer;
#endif
}

} // namespace backup
