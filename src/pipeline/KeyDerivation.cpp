#include "pipeline/KeyDerivation.h"

#include <cstring>
#include <fstream>
#include <stdexcept>

namespace backup {

// ============================================================================
// Simple Iterative Key Derivation
// ============================================================================
//
// Uses a 64-byte internal state mixed through a lightweight round function.
// The mixing function is based on 32-bit arithmetic (add, xor, rotate) and
// is inspired by the FNV-1a hash and basic sponge constructions.
//
// Security note: This is a simplified KDF intended for a self-contained
// backup tool. It provides basic key stretching but should not be relied
// upon for high-security applications. The iteration count provides a
// tunable work factor against brute-force attacks.
// ============================================================================

namespace {

// 32-bit left rotation
inline uint32_t rotl32(uint32_t x, int n) {
    return (x << n) | (x >> (32 - n));
}

// Mix the 64-byte state using a simple round function
// The state is treated as 16 uint32_t values
void mixState(uint8_t* state, size_t /*stateSize*/) {
    // Treat state as an array of uint32_t (stateSize must be 64 = 16 * 4)
    uint32_t* s = reinterpret_cast<uint32_t*>(state);
    constexpr int N = 16;  // 64 bytes / 4 bytes per uint32_t

    for (int round = 0; round < 8; ++round) {
        for (int i = 0; i < N; ++i) {
            const int next = (i + 1) % N;
            const int prev = (i + N - 1) % N;

            // Mix with neighbors using add, xor, and rotate
            s[i] ^= rotl32(s[next], 7);
            s[i] += s[prev];
            s[i] ^= rotl32(s[i], 13);
            s[i] += s[next] ^ static_cast<uint32_t>(0x9E3779B9);  // golden ratio
        }
    }
}

// Initialize the 64-byte state from password bytes, salt, and iteration counter
void initState(uint8_t* state, size_t stateSize,
               const uint8_t* password, size_t passwordLen,
               const uint8_t* salt, size_t saltLen,
               unsigned int iteration) {
    std::memset(state, 0, stateSize);

    // Fill state with password, salt, and iteration counter interleaved
    for (size_t i = 0; i < stateSize; ++i) {
        state[i] ^= password[i % passwordLen];
        state[i] ^= salt[i % saltLen];
        state[i] ^= static_cast<uint8_t>((iteration >> ((i % 4) * 8)) & 0xFF);
    }
}

}  // namespace

std::vector<uint8_t> KeyDerivation::deriveKey(const std::string& password,
                                                const uint8_t* salt,
                                                size_t saltSize,
                                                size_t keySize,
                                                unsigned int iterations) {
    if (password.empty()) {
        throw std::invalid_argument("KeyDerivation: password must not be empty");
    }
    if (salt == nullptr || saltSize == 0) {
        throw std::invalid_argument("KeyDerivation: salt must be provided");
    }
    if (keySize == 0) {
        return {};
    }

    const auto* passwordBytes = reinterpret_cast<const uint8_t*>(password.data());
    const size_t passwordLen = password.size();

    // 64-byte internal state
    constexpr size_t STATE_SIZE = 64;
    uint8_t state[STATE_SIZE];

    std::vector<uint8_t> key(keySize);
    size_t generated = 0;

    unsigned int iterCounter = 0;

    while (generated < keySize) {
        // Initialize state with password, salt, and current iteration counter
        initState(state, STATE_SIZE, passwordBytes, passwordLen,
                  salt, saltSize, iterCounter);

        // Apply the mixing function iteratively
        for (unsigned int i = 0; i < iterations; ++i) {
            // Fold the iteration number into the state periodically
            if (i % 100 == 0) {
                for (int j = 0; j < 4; ++j) {
                    state[j] ^= static_cast<uint8_t>((i >> (j * 8)) & 0xFF);
                }
            }
            mixState(state, STATE_SIZE);
        }

        // Extract bytes from the state
        const size_t toCopy = std::min(STATE_SIZE, keySize - generated);
        std::memcpy(key.data() + generated, state, toCopy);
        generated += toCopy;
        ++iterCounter;
    }

    return key;
}

std::vector<uint8_t> KeyDerivation::generateRandomBytes(size_t count) {
    if (count == 0) {
        return {};
    }

    std::vector<uint8_t> buffer(count);
    std::ifstream urandom("/dev/urandom", std::ios::binary);
    if (!urandom.read(reinterpret_cast<char*>(buffer.data()),
                       static_cast<std::streamsize>(count))) {
        throw std::runtime_error("KeyDerivation: failed to read from /dev/urandom");
    }
    return buffer;
}

} // namespace backup
