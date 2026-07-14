/**
 * @file test_key_derivation.cpp
 * @brief Google Test unit tests for KeyDerivation
 *
 * Tests PBKDF2 key derivation, random byte generation, and secure buffer
 * clearing. Only compiled when HAS_OPENSSL is defined.
 */

#include "pipeline/KeyDerivation.h"

#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

namespace backup {
namespace {

// ============================================================================
// deriveKey tests
// ============================================================================

TEST(KeyDerivationTest, DeriveKeyReturnsCorrectSize) {
    std::vector<uint8_t> salt = KeyDerivation::generateRandomBytes(16);

    auto key = KeyDerivation::deriveKey("test_password", salt.data(), salt.size(), 32, 1000);
    EXPECT_EQ(key.size(), 32);
}

TEST(KeyDerivationTest, DeriveKeyCustomSize) {
    std::vector<uint8_t> salt = KeyDerivation::generateRandomBytes(16);

    auto key = KeyDerivation::deriveKey("password", salt.data(), salt.size(), 16, 1000);
    EXPECT_EQ(key.size(), 16);
}

TEST(KeyDerivationTest, DeriveKeyDeterministic) {
    std::vector<uint8_t> salt(16, 0x42);

    auto key1 = KeyDerivation::deriveKey("same_pass", salt.data(), salt.size(), 32, 1000);
    auto key2 = KeyDerivation::deriveKey("same_pass", salt.data(), salt.size(), 32, 1000);

    EXPECT_EQ(key1, key2);
}

TEST(KeyDerivationTest, DeriveKeyDifferentPasswordDifferentKey) {
    std::vector<uint8_t> salt(16, 0x42);

    auto key1 = KeyDerivation::deriveKey("password1", salt.data(), salt.size(), 32, 1000);
    auto key2 = KeyDerivation::deriveKey("password2", salt.data(), salt.size(), 32, 1000);

    EXPECT_NE(key1, key2);
}

TEST(KeyDerivationTest, DeriveKeyDifferentSaltDifferentKey) {
    std::vector<uint8_t> salt1(16, 0x42);
    std::vector<uint8_t> salt2(16, 0x84);

    auto key1 = KeyDerivation::deriveKey("password", salt1.data(), salt1.size(), 32, 1000);
    auto key2 = KeyDerivation::deriveKey("password", salt2.data(), salt2.size(), 32, 1000);

    EXPECT_NE(key1, key2);
}

TEST(KeyDerivationTest, DeriveKeyDifferentIterationsDifferentKey) {
    std::vector<uint8_t> salt(16, 0x42);

    auto key1 = KeyDerivation::deriveKey("password", salt.data(), salt.size(), 32, 1000);
    auto key2 = KeyDerivation::deriveKey("password", salt.data(), salt.size(), 32, 2000);

    EXPECT_NE(key1, key2);
}

TEST(KeyDerivationTest, DeriveKeyEmptyPasswordThrows) {
    std::vector<uint8_t> salt(16, 0x42);

    EXPECT_THROW(
        KeyDerivation::deriveKey("", salt.data(), salt.size(), 32, 1000),
        std::invalid_argument
    );
}

TEST(KeyDerivationTest, DeriveKeyNullSaltThrows) {
    EXPECT_THROW(
        KeyDerivation::deriveKey("password", nullptr, 0, 32, 1000),
        std::invalid_argument
    );
}

// ============================================================================
// generateRandomBytes tests
// ============================================================================

TEST(KeyDerivationTest, GenerateRandomBytesCorrectSize) {
    auto bytes = KeyDerivation::generateRandomBytes(16);
    EXPECT_EQ(bytes.size(), 16);

    bytes = KeyDerivation::generateRandomBytes(32);
    EXPECT_EQ(bytes.size(), 32);

    bytes = KeyDerivation::generateRandomBytes(64);
    EXPECT_EQ(bytes.size(), 64);
}

TEST(KeyDerivationTest, GenerateRandomBytesNotAllZero) {
    // Statistically, generating 64 random bytes should not all be zero
    auto bytes = KeyDerivation::generateRandomBytes(64);
    bool allZero = true;
    for (uint8_t b : bytes) {
        if (b != 0) {
            allZero = false;
            break;
        }
    }
    EXPECT_FALSE(allZero);
}

TEST(KeyDerivationTest, GenerateRandomBytesDifferentEachCall) {
    auto bytes1 = KeyDerivation::generateRandomBytes(32);
    auto bytes2 = KeyDerivation::generateRandomBytes(32);

    // Should be different with overwhelming probability
    EXPECT_NE(bytes1, bytes2);
}

TEST(KeyDerivationTest, GenerateZeroRandomBytes) {
    auto bytes = KeyDerivation::generateRandomBytes(0);
    EXPECT_TRUE(bytes.empty());
}

// ============================================================================
// secureClear tests
// ============================================================================

TEST(KeyDerivationTest, SecureClearZerosBuffer) {
    std::vector<uint8_t> data = {0x01, 0x02, 0x03, 0x04, 0x05};
    EXPECT_FALSE(data.empty());

    KeyDerivation::secureClear(data);

    for (uint8_t byte : data) {
        EXPECT_EQ(byte, 0);
    }
}

TEST(KeyDerivationTest, SecureClearHandlesEmptyBuffer) {
    std::vector<uint8_t> empty;
    // Should not crash
    KeyDerivation::secureClear(empty);
    EXPECT_TRUE(empty.empty());
}

TEST(KeyDerivationTest, SecureClearHandlesLargeBuffer) {
    std::vector<uint8_t> data(4096, 0xFF);
    KeyDerivation::secureClear(data);

    for (uint8_t byte : data) {
        EXPECT_EQ(byte, 0);
    }
}

// ============================================================================
// Integration test: deriveKey + generateRandomBytes
// ============================================================================

TEST(KeyDerivationTest, DeriveWithGeneratedSalt) {
    // Full flow: generate random salt, derive key, verify size
    auto salt = KeyDerivation::generateRandomBytes(16);
    ASSERT_EQ(salt.size(), 16);

    auto key = KeyDerivation::deriveKey("MySecretPassword", salt.data(), salt.size());
    ASSERT_EQ(key.size(), 32);

    // Key should not be all zeros
    bool allZero = true;
    for (uint8_t b : key) {
        if (b != 0) { allZero = false; break; }
    }
    EXPECT_FALSE(allZero);
}

}  // namespace
}  // namespace backup
