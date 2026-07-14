/**
 * @file test_stream_packer.cpp
 * @brief Google Test unit tests for StreamPacker
 *
 * Tests pack/unpack roundtrip, data integrity, edge cases, and move semantics.
 */

#include "pipeline/StreamPacker.h"

#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

namespace backup {
namespace {

// ============================================================================
// Helper utilities
// ============================================================================

FileMetaData makeMeta(const std::string& path,
                      bool isDir = false,
                      uint64_t size = 0) {
    FileMetaData meta{};
    meta.relativePath = path;
    meta.isDirectory = isDir;
    meta.fileSize = size;
    meta.permissions = 0644;
    meta.ownerId = 1000;
    meta.groupId = 1000;
    meta.accessTime = 1000000;
    meta.modifyTime = 2000000;
    return meta;
}

std::vector<uint8_t> makeData(size_t size) {
    std::vector<uint8_t> data(size);
    for (size_t i = 0; i < size; ++i) {
        data[i] = static_cast<uint8_t>(i % 256);
    }
    return data;
}

// Helper: pack a single regular file and feed data to unpack
struct PackedResult {
    std::vector<uint8_t> stream;
    FileMetaData meta;
};

PackedResult packSingleFile(const std::string& path, const std::vector<uint8_t>& data) {
    StreamPacker packer;
    auto meta = makeMeta(path, false, data.size());
    auto header = packer.packHeader(meta);
    auto body = packer.packData(data.data(), data.size());

    std::vector<uint8_t> stream;
    stream.insert(stream.end(), header.begin(), header.end());
    stream.insert(stream.end(), body.begin(), body.end());
    return {stream, meta};
}

// ============================================================================
// Pack tests (write side)
// ============================================================================

TEST(StreamPackerTest, PackHeaderProducesValidBytes) {
    StreamPacker packer;
    auto meta = makeMeta("docs/readme.txt", false, 4096);
    auto header = packer.packHeader(meta);
    ASSERT_FALSE(header.empty());
    EXPECT_GE(header.size(), sizeof(FileEntryHeader));
    EXPECT_EQ(header.size(), sizeof(FileEntryHeader) + meta.relativePath.size());
}

TEST(StreamPackerTest, PackDataReturnsSameBytes) {
    StreamPacker packer;
    auto input = makeData(1000);
    auto output = packer.packData(input.data(), input.size());
    ASSERT_EQ(output.size(), input.size());
    EXPECT_EQ(output, input);
}

TEST(StreamPackerTest, PackDataEmptyInput) {
    StreamPacker packer;
    auto result = packer.packData(nullptr, 0);
    EXPECT_TRUE(result.empty());
}

TEST(StreamPackerTest, EndPackReturnsEmpty) {
    StreamPacker packer;
    auto result = packer.endPack();
    EXPECT_TRUE(result.empty());
}

TEST(StreamPackerTest, PackDirectoryHeader) {
    StreamPacker packer;
    auto meta = makeMeta("subdir", true, 0);
    auto header = packer.packHeader(meta);
    ASSERT_FALSE(header.empty());

    // Parse back the header to verify
    FileEntryHeader entryHeader{};
    std::memcpy(&entryHeader, header.data(), sizeof(FileEntryHeader));
    EXPECT_NE(entryHeader.isDirectory, 0);
    EXPECT_EQ(entryHeader.pathLength, static_cast<uint16_t>(meta.relativePath.size()));

    std::string path(entryHeader.pathLength, '\0');
    std::memcpy(&path[0], header.data() + sizeof(FileEntryHeader), entryHeader.pathLength);
    EXPECT_EQ(path, "subdir");
}

// ============================================================================
// Unpack tests (read side)
// ============================================================================

TEST(StreamPackerTest, UnpackSingleFile) {
    auto [stream, originalMeta] = packSingleFile("file.txt", makeData(500));
    ASSERT_FALSE(stream.empty());

    StreamPacker packer;
    packer.feedData(stream.data(), stream.size());

    FileMetaData parsed{};
    ASSERT_TRUE(packer.tryReadFileMeta(parsed));
    EXPECT_EQ(parsed.relativePath, "file.txt");
    EXPECT_EQ(parsed.fileSize, 500);
    EXPECT_FALSE(parsed.isDirectory);

    // Read file data
    std::vector<uint8_t> output(500);
    ssize_t bytesRead = packer.readFileData(output.data(), 500);
    ASSERT_EQ(bytesRead, 500);
    EXPECT_EQ(output, makeData(500));

    // No more data
    EXPECT_EQ(packer.readFileData(output.data(), 100), 0);
}

TEST(StreamPackerTest, UnpackSingleFileInMultipleReads) {
    auto [stream, originalMeta] = packSingleFile("file.txt", makeData(1000));
    ASSERT_FALSE(stream.empty());

    StreamPacker packer;
    packer.feedData(stream.data(), stream.size());

    FileMetaData parsed{};
    ASSERT_TRUE(packer.tryReadFileMeta(parsed));

    // Read in small chunks
    std::vector<uint8_t> output(1000);
    size_t totalRead = 0;
    while (totalRead < 1000) {
        ssize_t n = packer.readFileData(output.data() + totalRead, 100);
        ASSERT_GT(n, 0);
        totalRead += static_cast<size_t>(n);
    }
    EXPECT_EQ(totalRead, 1000);
    EXPECT_EQ(output, makeData(1000));
}

TEST(StreamPackerTest, UnpackMultipleFiles) {
    StreamPacker packer;

    // Pack file 1
    auto meta1 = makeMeta("a.txt", false, 300);
    auto data1 = makeData(300);
    auto header1 = packer.packHeader(meta1);
    auto body1 = packer.packData(data1.data(), data1.size());

    // Pack file 2
    auto meta2 = makeMeta("b.txt", false, 500);
    auto data2 = makeData(500);
    auto header2 = packer.packHeader(meta2);
    auto body2 = packer.packData(data2.data(), data2.size());

    // Combine into stream
    std::vector<uint8_t> stream;
    stream.insert(stream.end(), header1.begin(), header1.end());
    stream.insert(stream.end(), body1.begin(), body1.end());
    stream.insert(stream.end(), header2.begin(), header2.end());
    stream.insert(stream.end(), body2.begin(), body2.end());

    // Unpack
    StreamPacker unpacker;
    unpacker.feedData(stream.data(), stream.size());

    FileMetaData parsed1{};
    ASSERT_TRUE(unpacker.tryReadFileMeta(parsed1));
    EXPECT_EQ(parsed1.relativePath, "a.txt");
    EXPECT_EQ(parsed1.fileSize, 300);

    std::vector<uint8_t> out1(300);
    ASSERT_EQ(unpacker.readFileData(out1.data(), 300), 300);
    EXPECT_EQ(out1, data1);

    FileMetaData parsed2{};
    ASSERT_TRUE(unpacker.tryReadFileMeta(parsed2));
    EXPECT_EQ(parsed2.relativePath, "b.txt");
    EXPECT_EQ(parsed2.fileSize, 500);

    std::vector<uint8_t> out2(500);
    ASSERT_EQ(unpacker.readFileData(out2.data(), 500), 500);
    EXPECT_EQ(out2, data2);

    // No more files
    FileMetaData parsed3{};
    EXPECT_FALSE(unpacker.tryReadFileMeta(parsed3));
}

TEST(StreamPackerTest, UnpackMixedFilesAndDirectories) {
    StreamPacker packer;

    // Pack directory
    auto dirMeta = makeMeta("docs", true, 0);
    auto dirHeader = packer.packHeader(dirMeta);

    // Pack file
    auto fileMeta = makeMeta("docs/readme.md", false, 100);
    auto fileData = makeData(100);
    auto fileHeader = packer.packHeader(fileMeta);
    auto fileBody = packer.packData(fileData.data(), fileData.size());

    // Combine
    std::vector<uint8_t> stream;
    stream.insert(stream.end(), dirHeader.begin(), dirHeader.end());
    stream.insert(stream.end(), fileHeader.begin(), fileHeader.end());
    stream.insert(stream.end(), fileBody.begin(), fileBody.end());

    // Unpack
    StreamPacker unpacker;
    unpacker.feedData(stream.data(), stream.size());

    FileMetaData parsed1{};
    ASSERT_TRUE(unpacker.tryReadFileMeta(parsed1));
    EXPECT_EQ(parsed1.relativePath, "docs");
    EXPECT_TRUE(parsed1.isDirectory);

    FileMetaData parsed2{};
    ASSERT_TRUE(unpacker.tryReadFileMeta(parsed2));
    EXPECT_EQ(parsed2.relativePath, "docs/readme.md");
    EXPECT_FALSE(parsed2.isDirectory);
    EXPECT_EQ(parsed2.fileSize, 100);

    std::vector<uint8_t> out(100);
    ASSERT_EQ(unpacker.readFileData(out.data(), 100), 100);
    EXPECT_EQ(out, fileData);
}

// ============================================================================
// Data integrity tests
// ============================================================================

TEST(StreamPackerTest, BinaryDataPreservation) {
    std::vector<uint8_t> data(256);
    for (size_t i = 0; i < 256; ++i) {
        data[i] = static_cast<uint8_t>(i);
    }

    auto [stream, meta] = packSingleFile("bin.bin", data);

    StreamPacker unpacker;
    unpacker.feedData(stream.data(), stream.size());

    FileMetaData parsed{};
    ASSERT_TRUE(unpacker.tryReadFileMeta(parsed));

    std::vector<uint8_t> out(256);
    ASSERT_EQ(unpacker.readFileData(out.data(), 256), 256);
    EXPECT_EQ(out, data);
}

TEST(StreamPackerTest, EmptyFileRoundtrip) {
    auto [stream, meta] = packSingleFile("empty.txt", {});

    StreamPacker unpacker;
    unpacker.feedData(stream.data(), stream.size());

    FileMetaData parsed{};
    ASSERT_TRUE(unpacker.tryReadFileMeta(parsed));
    EXPECT_EQ(parsed.fileSize, 0);

    // Read from empty file
    uint8_t buf[16];
    EXPECT_EQ(unpacker.readFileData(buf, 16), 0);
}

TEST(StreamPackerTest, LargeFileRoundtrip) {
    auto data = makeData(65536);  // 64KB
    auto [stream, meta] = packSingleFile("large.bin", data);

    StreamPacker unpacker;
    unpacker.feedData(stream.data(), stream.size());

    FileMetaData parsed{};
    ASSERT_TRUE(unpacker.tryReadFileMeta(parsed));

    std::vector<uint8_t> out(65536);
    size_t total = 0;
    while (total < 65536) {
        ssize_t n = unpacker.readFileData(out.data() + total, 4096);
        ASSERT_GE(n, 0);
        if (n == 0) break;
        total += static_cast<size_t>(n);
    }
    EXPECT_EQ(total, 65536);
    EXPECT_EQ(out, data);
}

// ============================================================================
// Edge case tests
// ============================================================================

TEST(StreamPackerTest, FeedEmptyData) {
    StreamPacker packer;
    packer.feedData(nullptr, 0);
    packer.feedData(nullptr, 100);

    FileMetaData meta{};
    EXPECT_FALSE(packer.tryReadFileMeta(meta));
}

TEST(StreamPackerTest, IncompleteHeader) {
    StreamPacker packer;
    // Feed fewer bytes than sizeof(FileEntryHeader)
    uint8_t partial[10] = {};
    packer.feedData(partial, 10);

    FileMetaData meta{};
    EXPECT_FALSE(packer.tryReadFileMeta(meta));
}

TEST(StreamPackerTest, IncompletePath) {
    StreamPacker packer;
    // Create a header with pathLength > available data
    FileEntryHeader header{};
    header.pathLength = 100;
    uint8_t raw[sizeof(FileEntryHeader)];
    std::memcpy(raw, &header, sizeof(FileEntryHeader));

    packer.feedData(raw, sizeof(FileEntryHeader));

    FileMetaData meta{};
    EXPECT_FALSE(packer.tryReadFileMeta(meta));  // Path bytes not yet available
}

TEST(StreamPackerTest, IsFinishedAfterValidEntries) {
    auto [stream, meta] = packSingleFile("f.txt", makeData(10));

    StreamPacker packer;
    packer.feedData(stream.data(), stream.size());

    FileMetaData parsed{};
    ASSERT_TRUE(packer.tryReadFileMeta(parsed));
    EXPECT_FALSE(packer.isFinished());

    // Read all data
    uint8_t buf[10];
    EXPECT_EQ(packer.readFileData(buf, 10), 10);

    // Next tryReadFileMeta should trigger finished_ (no more valid entries)
    FileMetaData next{};
    EXPECT_FALSE(packer.tryReadFileMeta(next));
    EXPECT_TRUE(packer.isFinished());
}

TEST(StreamPackerTest, ReadBeforeTryReadFileMeta) {
    StreamPacker packer;
    uint8_t buf[10];
    EXPECT_EQ(packer.readFileData(buf, 10), -1);  // No current file
}

TEST(StreamPackerTest, ReadZeroSize) {
    auto [stream, meta] = packSingleFile("f.txt", makeData(100));
    StreamPacker packer;
    packer.feedData(stream.data(), stream.size());

    FileMetaData parsed{};
    ASSERT_TRUE(packer.tryReadFileMeta(parsed));

    EXPECT_EQ(packer.readFileData(nullptr, 0), 0);
}

TEST(StreamPackerTest, ReadNullBufferNonZeroSize) {
    auto [stream, meta] = packSingleFile("f.txt", makeData(100));
    StreamPacker packer;
    packer.feedData(stream.data(), stream.size());

    FileMetaData parsed{};
    ASSERT_TRUE(packer.tryReadFileMeta(parsed));

    EXPECT_EQ(packer.readFileData(nullptr, 100), -1);
}

TEST(StreamPackerTest, ResetClearsAllState) {
    auto [stream, meta] = packSingleFile("f.txt", makeData(100));
    StreamPacker packer;
    packer.feedData(stream.data(), stream.size());

    FileMetaData parsed{};
    ASSERT_TRUE(packer.tryReadFileMeta(parsed));

    packer.reset();
    EXPECT_FALSE(packer.isFinished());

    // After reset, tryReadFileMeta should fail (no data buffered)
    EXPECT_FALSE(packer.tryReadFileMeta(parsed));
}

// ============================================================================
// Move semantics tests
// ============================================================================

TEST(StreamPackerTest, MoveConstructorWorks) {
    StreamPacker packer1;
    auto meta = makeMeta("test.txt", false, 200);
    auto header = packer1.packHeader(meta);
    EXPECT_FALSE(header.empty());

    StreamPacker packer2(std::move(packer1));

    // packer2 should work fine
    auto header2 = packer2.packHeader(meta);
    EXPECT_FALSE(header2.empty());
}

// ============================================================================
// Metadata roundtrip tests
// ============================================================================

TEST(StreamPackerTest, MetadataRoundtrip) {
    StreamPacker packer;
    auto original = makeMeta("path/to/file.txt", false, 10240);
    original.permissions = 0755;
    original.ownerId = 1000;
    original.groupId = 1000;
    original.accessTime = 1712345678;
    original.modifyTime = 1712345678;

    auto header = packer.packHeader(original);

    // Feed it back
    StreamPacker unpacker;
    auto data = makeData(10240);
    auto body = packer.packData(data.data(), data.size());

    std::vector<uint8_t> stream;
    stream.insert(stream.end(), header.begin(), header.end());
    stream.insert(stream.end(), body.begin(), body.end());

    unpacker.feedData(stream.data(), stream.size());

    FileMetaData roundtripped{};
    ASSERT_TRUE(unpacker.tryReadFileMeta(roundtripped));

    EXPECT_EQ(roundtripped.relativePath, original.relativePath);
    EXPECT_EQ(roundtripped.permissions, original.permissions);
    EXPECT_EQ(roundtripped.ownerId, original.ownerId);
    EXPECT_EQ(roundtripped.groupId, original.groupId);
    EXPECT_EQ(roundtripped.accessTime, original.accessTime);
    EXPECT_EQ(roundtripped.modifyTime, original.modifyTime);
    EXPECT_EQ(roundtripped.isDirectory, original.isDirectory);
    EXPECT_EQ(roundtripped.fileSize, original.fileSize);
}

}  // namespace
}  // namespace backup
