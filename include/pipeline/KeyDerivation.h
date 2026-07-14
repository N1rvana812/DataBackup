#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

namespace backup {

// ============================================================================
// Iterative Key Derivation for password-based encryption
// ============================================================================
//
// Implements a simple iterative key stretching algorithm based on a
// lightweight mixing function (inspired by the FNV-1a hash and basic
// sponge constructions). The algorithm repeatedly mixes password, salt,
// and a counter through a 32-bit state machine to produce derived key
// material of the requested length.
//
// The algorithm is:
//   1. Initialize a 64-byte state from password and salt
//   2. For each iteration, apply a mixing function to the state
//   3. Extract keySize bytes from the final state, cycling as needed
//
// This provides a self-contained key derivation without external
// cryptographic library dependencies.
// ============================================================================

class KeyDerivation {
public:
    // Derive a cryptographic key from a password and salt using iterative mixing.
    // @param password   The user-provided password
    // @param salt       16-byte random salt
    // @param keySize    Output key size in bytes (default 32)
    // @param iterations Number of mixing iterations (default 100000)
    // @return           Derived key bytes (keySize bytes)
    static std::vector<uint8_t> deriveKey(const std::string& password,
                                           const uint8_t* salt,
                                           size_t saltSize,
                                           size_t keySize = 32,
                                           unsigned int iterations = 100000);

    // Generate cryptographically secure random bytes
    static std::vector<uint8_t> generateRandomBytes(size_t count);

    // Securely clear a buffer (prevents compiler optimization from removing the clear)
    template<typename T>
    static void secureClear(std::vector<T>& buffer) {
        if (!buffer.empty()) {
            // Use volatile to prevent compiler from optimizing away the clearing
            volatile T* ptr = buffer.data();
            for (size_t i = 0; i < buffer.size(); ++i) {
                ptr[i] = 0;
            }
        }
    }
};

} // namespace backup
