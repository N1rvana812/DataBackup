/**
 * @file test_archive_format.cpp
 * @brief Google Test unit tests for ArchiveFormat helpers
 *
 * Tests global header init/validation, footer init/validation,
 * and FileEntryHeader ↔ FileMetaData conversion roundtrip.
 */

#include "pipeline/ArchiveFormat.h"

#include <gtest/gtest.h>

namespace backup {
namespace {

// ============================================================================
// ArchiveGlobalHeader tests
// ============================================================================

TEST(ArchiveGlobalHeaderTest, InitSetsMagicAndVersion) {
    ArchiveGlobalHeader header{};
    BackupConfig config;
    std::vector<uint8_t> salt(SALT_SIZE, 0xAA);
    std::vector<uint8_t> iv(IV_SIZE, 0xBB);

    ArchiveFormat::initGlobalHeader(header, config, salt, iv);

    EXPECT_EQ(std::memcmp(header.magic, ARCHIVE_MAGIC, MAGIC_SIZE), 0);
    EXPECT_EQ(header.version, ARCHIVE_VERSION);
}

TEST(ArchiveGlobalHeaderTest, InitSetsFlagsForCompression) {
    ArchiveGlobalHeader header{};
    BackupConfig config;
    config.enableCompression = true;
    std::vector<uint8_t> salt(SALT_SIZE, 0x00);
    std::vector<uint8_t> iv(IV_SIZE, 0x00);

    ArchiveFormat::initGlobalHeader(header, config, salt, iv);

    EXPECT_TRUE(header.flags & FLAG_COMPRESSION);
    EXPECT_FALSE(header.flags & FLAG_ENCRYPTION);
}

TEST(ArchiveGlobalHeaderTest, InitSetsFlagsForEncryption) {
    ArchiveGlobalHeader header{};
    BackupConfig config;
    config.enableEncryption = true;
    std::vector<uint8_t> salt(SALT_SIZE, 0x00);
    std::vector<uint8_t> iv(IV_SIZE, 0x00);

    ArchiveFormat::initGlobalHeader(header, config, salt, iv);

    EXPECT_TRUE(header.flags & FLAG_ENCRYPTION);
    EXPECT_FALSE(header.flags & FLAG_COMPRESSION);
}

TEST(ArchiveGlobalHeaderTest, InitSetsFlagsForBothCompressionAndEncryption) {
    ArchiveGlobalHeader header{};
    BackupConfig config;
    config.enableCompression = true;
    config.enableEncryption = true;
    std::vector<uint8_t> salt(SALT_SIZE, 0x00);
    std::vector<uint8_t> iv(IV_SIZE, 0x00);

    ArchiveFormat::initGlobalHeader(header, config, salt, iv);

    EXPECT_TRUE(header.flags & FLAG_COMPRESSION);
    EXPECT_TRUE(header.flags & FLAG_ENCRYPTION);
}

TEST(ArchiveGlobalHeaderTest, InitCopiesSaltAndIv) {
    ArchiveGlobalHeader header{};
    BackupConfig config;
    std::vector<uint8_t> salt(SALT_SIZE, 0x42);
    std::vector<uint8_t> iv(IV_SIZE, 0xDE);

    ArchiveFormat::initGlobalHeader(header, config, salt, iv);

    EXPECT_EQ(std::memcmp(header.salt, salt.data(), SALT_SIZE), 0);
    EXPECT_EQ(std::memcmp(header.iv, iv.data(), IV_SIZE), 0);
}

TEST(ArchiveGlobalHeaderTest, InitSetsCompressionLevel) {
    ArchiveGlobalHeader header{};
    BackupConfig config;
    config.compressionLevel = 9;
    std::vector<uint8_t> salt(SALT_SIZE, 0x00);
    std::vector<uint8_t> iv(IV_SIZE, 0x00);

    ArchiveFormat::initGlobalHeader(header, config, salt, iv);

    EXPECT_EQ(header.compressionLevel, 9);
}

TEST(ArchiveGlobalHeaderTest, InitClearsReservedBytes) {
    ArchiveGlobalHeader header{};
    BackupConfig config;
    std::vector<uint8_t> salt(SALT_SIZE, 0x00);
    std::vector<uint8_t> iv(IV_SIZE, 0x00);

    // Pre-fill reserved with garbage
    std::memset(header.reserved, 0xFF, sizeof(header.reserved));

    ArchiveFormat::initGlobalHeader(header, config, salt, iv);

    for (uint8_t byte : header.reserved) {
        EXPECT_EQ(byte, 0);
    }
}

// ============================================================================
// Magic validation tests
// ============================================================================

TEST(ArchiveFormatTest, ValidateMagicSucceeds) {
    char magic[4];
    std::memcpy(magic, ARCHIVE_MAGIC, MAGIC_SIZE);
    EXPECT_TRUE(ArchiveFormat::validateMagic(magic));
}

TEST(ArchiveFormatTest, ValidateMagicFailsOnBadMagic) {
    char magic[4] = {'B', 'A', 'D', '!'};
    EXPECT_FALSE(ArchiveFormat::validateMagic(magic));
}

TEST(ArchiveFormatTest, ValidateFooterMagicSucceeds) {
    char magic[4];
    std::memcpy(magic, ARCHIVE_FOOTER_MAGIC, MAGIC_SIZE);
    EXPECT_TRUE(ArchiveFormat::validateFooterMagic(magic));
}

TEST(ArchiveFormatTest, ValidateFooterMagicFailsOnBadMagic) {
    char magic[4] = {'B', 'A', 'D', '!'};
    EXPECT_FALSE(ArchiveFormat::validateFooterMagic(magic));
}

// ============================================================================
// ArchiveFooter tests
// ============================================================================

TEST(ArchiveFooterTest, InitSetsFields) {
    ArchiveFooter footer{};
    ArchiveFormat::initFooter(footer, 1024, 42);

    EXPECT_EQ(footer.indexOffset, 1024);
    EXPECT_EQ(footer.fileCount, 42);
    EXPECT_EQ(std::memcmp(footer.footerMagic, ARCHIVE_FOOTER_MAGIC, MAGIC_SIZE), 0);
}

// ============================================================================
// FileEntryHeader ↔ FileMetaData roundtrip
// ============================================================================

TEST(FileEntryRoundtripTest, MetaDataToHeaderAndBack) {
    FileMetaData original{};
    original.relativePath = "docs/readme.txt";
    original.permissions = 0644;
    original.ownerId = 1000;
    original.groupId = 1000;
    original.accessTime = 1712345678;
    original.modifyTime = 1712345678;
    original.isDirectory = false;
    original.fileSize = 4096;

    // Convert to header
    FileEntryHeader header{};
    ArchiveFormat::metaDataToFileEntryHeader(original, header);

    // Convert back
    FileMetaData roundtripped{};
    ArchiveFormat::fileEntryHeaderToMetaData(header, original.relativePath, roundtripped);

    EXPECT_EQ(roundtripped.relativePath, original.relativePath);
    EXPECT_EQ(roundtripped.permissions, original.permissions);
    EXPECT_EQ(roundtripped.ownerId, original.ownerId);
    EXPECT_EQ(roundtripped.groupId, original.groupId);
    EXPECT_EQ(roundtripped.accessTime, original.accessTime);
    EXPECT_EQ(roundtripped.modifyTime, original.modifyTime);
    EXPECT_EQ(roundtripped.isDirectory, original.isDirectory);
    EXPECT_EQ(roundtripped.fileSize, original.fileSize);
}

TEST(FileEntryRoundtripTest, DirectoryRoundtrip) {
    FileMetaData original{};
    original.relativePath = "subdir";
    original.permissions = 0755;
    original.ownerId = 0;
    original.groupId = 0;
    original.accessTime = 1000000;
    original.modifyTime = 2000000;
    original.isDirectory = true;
    original.fileSize = 0;

    FileEntryHeader header{};
    ArchiveFormat::metaDataToFileEntryHeader(original, header);

    FileMetaData roundtripped{};
    ArchiveFormat::fileEntryHeaderToMetaData(header, original.relativePath, roundtripped);

    EXPECT_TRUE(roundtripped.isDirectory);
    EXPECT_EQ(roundtripped.fileSize, 0);
    EXPECT_EQ(roundtripped.permissions, 0755);
}

TEST(FileEntryRoundtripTest, PathLengthMatches) {
    FileMetaData meta{};
    meta.relativePath = "very/long/path/to/a/file.txt";

    FileEntryHeader header{};
    ArchiveFormat::metaDataToFileEntryHeader(meta, header);

    EXPECT_EQ(header.pathLength, static_cast<uint16_t>(meta.relativePath.size()));
}

// ============================================================================
// Struct size assertions (compile-time)
// ============================================================================

TEST(ArchiveFormatSizeTest, GlobalHeaderSize) {
    EXPECT_EQ(sizeof(ArchiveGlobalHeader), 48);
}

TEST(ArchiveFormatSizeTest, FileEntryHeaderSize) {
    EXPECT_EQ(sizeof(FileEntryHeader), 39);
}

TEST(ArchiveFormatSizeTest, FooterSize) {
    EXPECT_EQ(sizeof(ArchiveFooter), 16);
}

}  // namespace
}  // namespace backup
