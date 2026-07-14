/**
 * @file test_stream_compressor.cpp
 * @brief Google Test unit tests for StreamCompressor
 *
 * Tests compress/decompress roundtrip, edge cases, and various data patterns.
 */

#include "pipeline/StreamCompressor.h"
#include "pipeline/ArchiveFormat.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace backup {
namespace {

// ============================================================================
// Helper utilities
// ============================================================================

/// Create a buffer filled with repeating pattern
std::vector<uint8_t> makePattern(size_t size, uint8_t seed = 0xAA) {
    std::vector<uint8_t> data(size);
    for (size_t i = 0; i < size; ++i) {
        data[i] = static_cast<uint8_t>(seed + i % 251);
    }
    return data;
}

/// Create a buffer filled with a single repeated byte value (highly compressible)
std::vector<uint8_t> makeRepeated(size_t size, uint8_t value = 0x00) {
    return std::vector<uint8_t>(size, value);
}

/// Create a buffer with random-like content (poorly compressible)
std::vector<uint8_t> makePseudoRandom(size_t size) {
    std::vector<uint8_t> data(size);
    uint32_t state = 12345;
    for (size_t i = 0; i < size; ++i) {
        state = state * 1103515245 + 12345;
        data[i] = static_cast<uint8_t>((state >> 16) & 0xFF);
    }
    return data;
}

// ============================================================================
// Roundtrip tests (compress then decompress)
// ============================================================================

TEST(StreamCompressorTest, RoundtripSmallData) {
    StreamCompressor comp;
    auto input = makePattern(100);

    auto compressed = comp.compress(input.data(), input.size());
    ASSERT_FALSE(compressed.empty());

    auto decompressed = comp.decompress(compressed.data(), compressed.size(),
                                        input.size());
    ASSERT_EQ(decompressed.size(), input.size());
    EXPECT_EQ(decompressed, input);
}

TEST(StreamCompressorTest, Roundtrip4KBChunk) {
    StreamCompressor comp;
    auto input = makePattern(CHUNK_SIZE);

    auto compressed = comp.compress(input.data(), input.size());
    ASSERT_FALSE(compressed.empty());

    auto decompressed = comp.decompress(compressed.data(), compressed.size(),
                                        input.size());
    ASSERT_EQ(decompressed.size(), input.size());
    EXPECT_EQ(decompressed, input);
}

TEST(StreamCompressorTest, RoundtripLargeData) {
    StreamCompressor comp;
    auto input = makePattern(65536);  // 64KB

    auto compressed = comp.compress(input.data(), input.size());
    ASSERT_FALSE(compressed.empty());

    auto decompressed = comp.decompress(compressed.data(), compressed.size(),
                                        input.size());
    ASSERT_EQ(decompressed.size(), input.size());
    EXPECT_EQ(decompressed, input);
}

TEST(StreamCompressorTest, RoundtripHighlyCompressibleData) {
    StreamCompressor comp(9);  // max compression
    auto input = makeRepeated(8192, 0x00);

    auto compressed = comp.compress(input.data(), input.size());
    ASSERT_FALSE(compressed.empty());

    // Highly compressible data should be much smaller
    EXPECT_LT(compressed.size(), input.size() / 4);

    auto decompressed = comp.decompress(compressed.data(), compressed.size(),
                                        input.size());
    ASSERT_EQ(decompressed.size(), input.size());
    EXPECT_EQ(decompressed, input);
}

TEST(StreamCompressorTest, RoundtripIncompressibleData) {
    StreamCompressor comp;
    auto input = makePseudoRandom(4096);

    auto compressed = comp.compress(input.data(), input.size());
    ASSERT_FALSE(compressed.empty());

    auto decompressed = comp.decompress(compressed.data(), compressed.size(),
                                        input.size());
    ASSERT_EQ(decompressed.size(), input.size());
    EXPECT_EQ(decompressed, input);
}

// ============================================================================
// Compression level tests
// ============================================================================

TEST(StreamCompressorTest, CompressionLevel1Works) {
    StreamCompressor comp(1);
    auto input = makePattern(1024);
    auto compressed = comp.compress(input.data(), input.size());
    EXPECT_FALSE(compressed.empty());
}

TEST(StreamCompressorTest, CompressionLevel9Works) {
    StreamCompressor comp(9);
    auto input = makePattern(1024);
    auto compressed = comp.compress(input.data(), input.size());
    EXPECT_FALSE(compressed.empty());
}

TEST(StreamCompressorTest, CompressionLevelClamped) {
    // Invalid levels should be clamped to [1, 9] by the constructor
    StreamCompressor compLow(0);
    auto input = makePattern(256);
    auto compressed = compLow.compress(input.data(), input.size());
    EXPECT_FALSE(compressed.empty());

    StreamCompressor compHigh(99);
    compressed = compHigh.compress(input.data(), input.size());
    EXPECT_FALSE(compressed.empty());
}

TEST(StreamCompressorTest, HigherLevelProducesBetterCompression) {
    StreamCompressor compFast(1);
    StreamCompressor compBest(9);
    auto input = makeRepeated(16384, 0x55);  // highly compressible

    auto fastResult = compFast.compress(input.data(), input.size());
    auto bestResult = compBest.compress(input.data(), input.size());

    // Level 9 should produce equal or better compression than level 1
    EXPECT_LE(bestResult.size(), fastResult.size());
}

// ============================================================================
// Edge case tests
// ============================================================================

TEST(StreamCompressorTest, EmptyDataCompress) {
    StreamCompressor comp;
    auto result = comp.compress(nullptr, 0);
    EXPECT_TRUE(result.empty());
}

TEST(StreamCompressorTest, NullDataWithSizeZero) {
    StreamCompressor comp;
    auto result = comp.compress(nullptr, 0);
    EXPECT_TRUE(result.empty());
}

TEST(StreamCompressorTest, NullDataWithNonZeroSize) {
    StreamCompressor comp;
    auto result = comp.compress(nullptr, 100);
    EXPECT_TRUE(result.empty());
}

TEST(StreamCompressorTest, EmptyDataDecompress) {
    StreamCompressor comp;
    auto result = comp.decompress(nullptr, 0, 0);
    EXPECT_TRUE(result.empty());
}

TEST(StreamCompressorTest, DecompressSingleByte) {
    StreamCompressor comp;
    std::vector<uint8_t> input = {0x42};

    auto compressed = comp.compress(input.data(), input.size());
    ASSERT_FALSE(compressed.empty());

    auto decompressed = comp.decompress(compressed.data(), compressed.size(),
                                        input.size());
    ASSERT_EQ(decompressed.size(), 1);
    EXPECT_EQ(decompressed[0], 0x42);
}

// ============================================================================
// Data integrity tests
// ============================================================================

TEST(StreamCompressorTest, BinaryDataPreservation) {
    StreamCompressor comp;
    std::vector<uint8_t> input(256);
    for (size_t i = 0; i < input.size(); ++i) {
        input[i] = static_cast<uint8_t>(i);  // 0x00 through 0xFF
    }

    auto compressed = comp.compress(input.data(), input.size());
    auto decompressed = comp.decompress(compressed.data(), compressed.size(),
                                        input.size());
    EXPECT_EQ(decompressed, input);
}

TEST(StreamCompressorTest, TextDataRoundtrip) {
    StreamCompressor comp;
    const std::string text =
        "Lorem ipsum dolor sit amet, consectetur adipiscing elit. "
        "Sed do eiusmod tempor incididunt ut labore et dolore magna aliqua. "
        "This text should compress well because it has repeating patterns.";
    std::vector<uint8_t> input(text.begin(), text.end());

    auto compressed = comp.compress(input.data(), input.size());
    auto decompressed = comp.decompress(compressed.data(), compressed.size(),
                                        input.size());

    std::string result(decompressed.begin(), decompressed.end());
    EXPECT_EQ(result, text);
}

// ============================================================================
// Move semantics
// ============================================================================

TEST(StreamCompressorTest, MoveConstructorWorks) {
    StreamCompressor comp1(5);
    auto input = makePattern(256);
    auto compressed = comp1.compress(input.data(), input.size());

    StreamCompressor comp2(std::move(comp1));

    auto decompressed = comp2.decompress(compressed.data(), compressed.size(),
                                         input.size());
    EXPECT_EQ(decompressed, input);
}

}  // namespace
}  // namespace backup
