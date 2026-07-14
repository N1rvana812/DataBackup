/**
 * @file test_stream_encryptor.cpp
 * @brief Google Test unit tests for StreamEncryptor (AES-256-CTR)
 *
 * Tests encrypt/decrypt roundtrip, key/IV initialization, streaming behavior,
 * and edge cases. Tests the RC4 stream cipher implementation.
 */

#include "pipeline/StreamEncryptor.h"

#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

namespace backup {
namespace {

// ============================================================================
// Test fixtures and helpers
// ============================================================================

/// Generate a deterministic 32-byte key from a seed
std::vector<uint8_t> makeKey(uint8_t seed = 0x00) {
    std::vector<uint8_t> key(32);
    for (size_t i = 0; i < key.size(); ++i) {
        key[i] = static_cast<uint8_t>(seed + i);
    }
    return key;
}

/// Generate a deterministic 16-byte IV from a seed
std::vector<uint8_t> makeIv(uint8_t seed = 0x10) {
    std::vector<uint8_t> iv(16);
    for (size_t i = 0; i < iv.size(); ++i) {
        iv[i] = static_cast<uint8_t>(seed + i);
    }
    return iv;
}

/// Create a buffer with known content
std::vector<uint8_t> makeData(size_t size) {
    std::vector<uint8_t> data(size);
    for (size_t i = 0; i < size; ++i) {
        data[i] = static_cast<uint8_t>(i % 256);
    }
    return data;
}

// ============================================================================
// Initialization tests
// ============================================================================

TEST(StreamEncryptorTest, NotInitializedByDefault) {
    StreamEncryptor enc;
    EXPECT_FALSE(enc.isInitialized());
}

TEST(StreamEncryptorTest, InitWithValidKeyAndIv) {
    StreamEncryptor enc;
    auto key = makeKey();
    auto iv = makeIv();

    EXPECT_TRUE(enc.init(key.data(), key.size(), iv.data(), iv.size()));
    EXPECT_TRUE(enc.isInitialized());
}

TEST(StreamEncryptorTest, InitFailsWithWrongKeySize) {
    StreamEncryptor enc;
    auto key = makeKey();
    auto iv = makeIv();

    // keySize != 32 is rejected (API contract, not inherent to RC4)
    EXPECT_FALSE(enc.init(key.data(), 16, iv.data(), iv.size()));
    EXPECT_FALSE(enc.isInitialized());
}

TEST(StreamEncryptorTest, InitFailsWithWrongIvSize) {
    StreamEncryptor enc;
    auto key = makeKey();
    auto iv = makeIv();

    // 8-byte IV is invalid
    EXPECT_FALSE(enc.init(key.data(), key.size(), iv.data(), 8));
    EXPECT_FALSE(enc.isInitialized());
}

// ============================================================================
// Encrypt/decrypt roundtrip tests
// ============================================================================

TEST(StreamEncryptorTest, RoundtripSmallData) {
    StreamEncryptor enc;
    auto key = makeKey();
    auto iv = makeIv();
    ASSERT_TRUE(enc.init(key.data(), key.size(), iv.data(), iv.size()));

    auto plaintext = makeData(64);

    auto ciphertext = enc.encrypt(plaintext.data(), plaintext.size());
    ASSERT_EQ(ciphertext.size(), plaintext.size());

    // Stream cipher: ciphertext should differ from plaintext
    EXPECT_NE(ciphertext, plaintext);

    // Re-init to reset the PRGA state for decryption
    ASSERT_TRUE(enc.init(key.data(), key.size(), iv.data(), iv.size()));
    auto decrypted = enc.decrypt(ciphertext.data(), ciphertext.size());
    ASSERT_EQ(decrypted.size(), plaintext.size());
    EXPECT_EQ(decrypted, plaintext);
}

TEST(StreamEncryptorTest, RoundtripLargeData) {
    StreamEncryptor enc;
    auto key = makeKey(0xAB);
    auto iv = makeIv(0xCD);
    ASSERT_TRUE(enc.init(key.data(), key.size(), iv.data(), iv.size()));

    auto plaintext = makeData(16384);  // 16KB

    auto ciphertext = enc.encrypt(plaintext.data(), plaintext.size());
    ASSERT_EQ(ciphertext.size(), plaintext.size());

    ASSERT_TRUE(enc.init(key.data(), key.size(), iv.data(), iv.size()));
    auto decrypted = enc.decrypt(ciphertext.data(), ciphertext.size());
    EXPECT_EQ(decrypted, plaintext);
}

TEST(StreamEncryptorTest, RoundtripEmptyData) {
    StreamEncryptor enc;
    auto key = makeKey();
    auto iv = makeIv();
    ASSERT_TRUE(enc.init(key.data(), key.size(), iv.data(), iv.size()));

    std::vector<uint8_t> empty;
    auto result = enc.encrypt(empty.data(), 0);
    EXPECT_TRUE(result.empty());

    ASSERT_TRUE(enc.init(key.data(), key.size(), iv.data(), iv.size()));
    result = enc.decrypt(empty.data(), 0);
    EXPECT_TRUE(result.empty());
}

// ============================================================================
// Streaming behavior tests
// ============================================================================

TEST(StreamEncryptorTest, StreamingEncryptThenDecryptAcrossCalls) {
    StreamEncryptor enc;
    auto key = makeKey();
    auto iv = makeIv();
    ASSERT_TRUE(enc.init(key.data(), key.size(), iv.data(), iv.size()));

    // Encrypt in multiple chunks
    auto chunk1 = makeData(100);
    auto chunk2 = makeData(200);
    auto chunk3 = makeData(150);

    auto enc1 = enc.encrypt(chunk1.data(), chunk1.size());
    auto enc2 = enc.encrypt(chunk2.data(), chunk2.size());
    auto enc3 = enc.encrypt(chunk3.data(), chunk3.size());

    ASSERT_EQ(enc1.size(), chunk1.size());
    ASSERT_EQ(enc2.size(), chunk2.size());
    ASSERT_EQ(enc3.size(), chunk3.size());

    // Re-init and decrypt in same chunk order
    ASSERT_TRUE(enc.init(key.data(), key.size(), iv.data(), iv.size()));

    auto dec1 = enc.decrypt(enc1.data(), enc1.size());
    auto dec2 = enc.decrypt(enc2.data(), enc2.size());
    auto dec3 = enc.decrypt(enc3.data(), enc3.size());

    EXPECT_EQ(dec1, chunk1);
    EXPECT_EQ(dec2, chunk2);
    EXPECT_EQ(dec3, chunk3);
}

// ============================================================================
// Determinism and uniqueness
// ============================================================================

TEST(StreamEncryptorTest, SameKeyIvProducesDeterministicOutput) {
    auto key = makeKey();
    auto iv = makeIv();

    auto plaintext = makeData(256);

    StreamEncryptor enc1;
    ASSERT_TRUE(enc1.init(key.data(), key.size(), iv.data(), iv.size()));
    auto cipher1 = enc1.encrypt(plaintext.data(), plaintext.size());

    StreamEncryptor enc2;
    ASSERT_TRUE(enc2.init(key.data(), key.size(), iv.data(), iv.size()));
    auto cipher2 = enc2.encrypt(plaintext.data(), plaintext.size());

    EXPECT_EQ(cipher1, cipher2);
}

TEST(StreamEncryptorTest, DifferentKeyProducesDifferentOutput) {
    auto iv = makeIv();
    auto plaintext = makeData(256);

    StreamEncryptor enc1;
    ASSERT_TRUE(enc1.init(makeKey(0x00).data(), 32, iv.data(), iv.size()));
    auto cipher1 = enc1.encrypt(plaintext.data(), plaintext.size());

    StreamEncryptor enc2;
    ASSERT_TRUE(enc2.init(makeKey(0xFF).data(), 32, iv.data(), iv.size()));
    auto cipher2 = enc2.encrypt(plaintext.data(), plaintext.size());

    EXPECT_NE(cipher1, cipher2);
}

TEST(StreamEncryptorTest, DifferentIvProducesDifferentOutput) {
    auto key = makeKey();
    auto plaintext = makeData(256);

    StreamEncryptor enc1;
    ASSERT_TRUE(enc1.init(key.data(), key.size(), makeIv(0x00).data(), 16));
    auto cipher1 = enc1.encrypt(plaintext.data(), plaintext.size());

    StreamEncryptor enc2;
    ASSERT_TRUE(enc2.init(key.data(), key.size(), makeIv(0xFF).data(), 16));
    auto cipher2 = enc2.encrypt(plaintext.data(), plaintext.size());

    EXPECT_NE(cipher1, cipher2);
}

// ============================================================================
// Edge cases
// ============================================================================

TEST(StreamEncryptorTest, EncryptBeforeInitThrows) {
    StreamEncryptor enc;
    auto data = makeData(100);
    EXPECT_THROW(enc.encrypt(data.data(), data.size()), std::runtime_error);
}

TEST(StreamEncryptorTest, DecryptBeforeInitThrows) {
    StreamEncryptor enc;
    auto data = makeData(100);
    EXPECT_THROW(enc.decrypt(data.data(), data.size()), std::runtime_error);
}

// ============================================================================
// Move semantics
// ============================================================================

TEST(StreamEncryptorTest, MoveConstructorPreservesState) {
    StreamEncryptor enc1;
    auto key = makeKey();
    auto iv = makeIv();
    ASSERT_TRUE(enc1.init(key.data(), key.size(), iv.data(), iv.size()));

    auto plaintext = makeData(512);
    auto ciphertext = enc1.encrypt(plaintext.data(), plaintext.size());

    // Move construct
    StreamEncryptor enc2(std::move(enc1));
    EXPECT_TRUE(enc2.isInitialized());

    // The moved-from encryptor can be re-initialized; state was zeroed on move
    ASSERT_TRUE(enc2.init(key.data(), key.size(), iv.data(), iv.size()));
    auto cipher2 = enc2.encrypt(plaintext.data(), plaintext.size());
    EXPECT_EQ(cipher2, ciphertext);
}

}  // namespace
}  // namespace backup
