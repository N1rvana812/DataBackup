#pragma once

#include "common/Types.h"

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <string>
#include <vector>

namespace backup {

// ============================================================================
// Binary Archive Format Constants
// ============================================================================

constexpr const char ARCHIVE_MAGIC[] = "DBAK";
constexpr const char ARCHIVE_FOOTER_MAGIC[] = "KABD";
constexpr size_t MAGIC_SIZE = 4;
constexpr uint16_t ARCHIVE_VERSION = 1;
constexpr size_t SALT_SIZE = 16;       // PBKDF2 salt
constexpr size_t IV_SIZE = 16;         // AES-CTR IV
constexpr size_t AES_KEY_SIZE = 32;    // AES-256 key
constexpr size_t CHUNK_SIZE = 4096;    // 4KB streaming chunk

// Flags for the global header
enum ArchiveFlags : uint16_t {
    FLAG_COMPRESSION = 0x01,
    FLAG_ENCRYPTION  = 0x02,
};

// ============================================================================
// Global Header (padded to 48 bytes for alignment)
// ============================================================================

#pragma pack(push, 1)
struct ArchiveGlobalHeader {
    char magic[4];            // "DBAK"
    uint16_t version;         // 1
    uint16_t flags;           // bitmask of ArchiveFlags
    uint8_t salt[16];         // random salt for PBKDF2
    uint8_t iv[16];           // random IV for AES-CTR
    uint8_t compressionLevel; // 1-9, meaningful only when FLAG_COMPRESSION is set
    uint8_t reserved[7];      // padding to 48 bytes
};
#pragma pack(pop)

static_assert(sizeof(ArchiveGlobalHeader) == 48,
              "ArchiveGlobalHeader must be 48 bytes");

// ============================================================================
// Per-File Header (variable-length due to path)
// ============================================================================

#pragma pack(push, 1)
struct FileEntryHeader {
    uint16_t pathLength;      // length of the path that immediately follows
    // char path[pathLength]; // immediately after this struct
    uint64_t originalSize;
    uint32_t permissions;     // mode_t truncated to 32 bits
    uint32_t ownerId;
    uint32_t groupId;
    int64_t accessTime;
    int64_t modifyTime;
    uint8_t isDirectory;
};
#pragma pack(pop)

static_assert(sizeof(FileEntryHeader) == 39,
              "FileEntryHeader must be 39 bytes");

// ============================================================================
// Archive Footer
// ============================================================================

#pragma pack(push, 1)
struct ArchiveFooter {
    uint64_t indexOffset;     // file offset where file entries begin (after global header)
    uint32_t fileCount;       // total number of file entries
    char footerMagic[4];      // "KABD" (reversed for endianness sanity check)
};
#pragma pack(pop)

static_assert(sizeof(ArchiveFooter) == 16,
              "ArchiveFooter must be 16 bytes");

// ============================================================================
// Serialization helpers
// ============================================================================

namespace ArchiveFormat {

inline void initGlobalHeader(ArchiveGlobalHeader& header,
                              const BackupConfig& config,
                              const std::vector<uint8_t>& salt,
                              const std::vector<uint8_t>& iv) {
    std::memcpy(header.magic, ARCHIVE_MAGIC, MAGIC_SIZE);
    header.version = ARCHIVE_VERSION;
    header.flags = 0;
    if (config.enableCompression) {
        header.flags |= FLAG_COMPRESSION;
    }
    if (config.enableEncryption) {
        header.flags |= FLAG_ENCRYPTION;
    }
    std::memcpy(header.salt, salt.data(), SALT_SIZE);
    std::memcpy(header.iv, iv.data(), IV_SIZE);
    header.compressionLevel = static_cast<uint8_t>(config.compressionLevel);
    std::memset(header.reserved, 0, sizeof(header.reserved));
}

inline bool validateMagic(const char magic[4]) {
    return std::memcmp(magic, ARCHIVE_MAGIC, MAGIC_SIZE) == 0;
}

inline bool validateFooterMagic(const char magic[4]) {
    return std::memcmp(magic, ARCHIVE_FOOTER_MAGIC, MAGIC_SIZE) == 0;
}

inline void initFooter(ArchiveFooter& footer,
                        uint64_t indexOffset,
                        uint32_t fileCount) {
    footer.indexOffset = indexOffset;
    footer.fileCount = fileCount;
    std::memcpy(footer.footerMagic, ARCHIVE_FOOTER_MAGIC, MAGIC_SIZE);
}

inline void fileEntryHeaderToMetaData(const FileEntryHeader& header,
                                       const std::string& path,
                                       FileMetaData& meta) {
    meta.relativePath = path;
    meta.permissions = static_cast<mode_t>(header.permissions);
    meta.ownerId = static_cast<uid_t>(header.ownerId);
    meta.groupId = static_cast<gid_t>(header.groupId);
    meta.accessTime = static_cast<time_t>(header.accessTime);
    meta.modifyTime = static_cast<time_t>(header.modifyTime);
    meta.isDirectory = (header.isDirectory != 0);
    meta.fileSize = header.originalSize;
}

inline void metaDataToFileEntryHeader(const FileMetaData& meta,
                                       FileEntryHeader& header) {
    header.pathLength = static_cast<uint16_t>(meta.relativePath.size());
    header.originalSize = meta.fileSize;
    header.permissions = static_cast<uint32_t>(meta.permissions);
    header.ownerId = static_cast<uint32_t>(meta.ownerId);
    header.groupId = static_cast<uint32_t>(meta.groupId);
    header.accessTime = static_cast<int64_t>(meta.accessTime);
    header.modifyTime = static_cast<int64_t>(meta.modifyTime);
    header.isDirectory = meta.isDirectory ? 1 : 0;
}

} // namespace ArchiveFormat
} // namespace backup
