#pragma once

#include "common/Types.h"
#include "pipeline/ArchiveFormat.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace backup {

// ============================================================================
// Streaming Packer / Unpacker
// ============================================================================
//
// Packs multiple files into a single contiguous byte stream and unpacks them
// back. The packed format is a simple concatenation of per-file entries:
//
//   [FileEntryHeader (39 bytes)] [path (pathLength bytes)] [raw data (originalSize bytes)]
//
// No end-of-stream marker — data exhaustion signals completion.
//
// This works at the same pipeline level as StreamCompressor (RLE) and
// StreamEncryptor (RC4): the packed stream flows through compress → encrypt
// before being written to disk.
// ============================================================================

class StreamPacker {
public:
    StreamPacker() = default;
    ~StreamPacker() = default;

    StreamPacker(const StreamPacker&) = delete;
    StreamPacker& operator=(const StreamPacker&) = delete;
    StreamPacker(StreamPacker&&) noexcept = default;
    StreamPacker& operator=(StreamPacker&&) noexcept = default;

    // ========== Write (pack) side ==========

    // Pack a file header + path into the output format.
    // Returns (FileEntryHeader + path) bytes for feeding into compression/encryption.
    // @param meta  File metadata (path, permissions, size, etc.)
    // @return      Raw bytes representing the packed file header
    std::vector<uint8_t> packHeader(const FileMetaData& meta);

    // Pack raw file data. Returns the data bytes unchanged (no extra framing).
    // @param data   Input data buffer
    // @param size   Input data size in bytes
    // @return       Raw data bytes (same as input) for feeding into compression/encryption
    std::vector<uint8_t> packData(const uint8_t* data, size_t size);

    // Signal end of packing. Returns empty vector (no footer in this format).
    std::vector<uint8_t> endPack();

    // ========== Read (unpack) side ==========

    // Feed raw packed data (after decompression/decryption) into the unpacker.
    // Data is accumulated in an internal buffer for subsequent parsing.
    // @param data   Packed stream data
    // @param size   Data size in bytes
    void feedData(const uint8_t* data, size_t size);

    // Try to parse the next file entry from buffered data.
    // Returns true and fills 'meta' when a complete FileEntryHeader + path is available.
    // The caller should then use readFileData() to extract the file's raw bytes.
    // @param meta   [out] Filled with the parsed file metadata on success
    // @return       true if a complete file header was parsed
    bool tryReadFileMeta(FileMetaData& meta);

    // Read raw data for the current file into a user buffer.
    // Semantics match IFileReader::readChunk:
    //   >0 : bytes copied into buffer
    //    0 : end of current file (caller should call tryReadFileMeta next)
    //   -1 : error (e.g., no current file)
    // @param buffer  User-provided output buffer
    // @param size    Maximum bytes to read
    // @return        Bytes read, 0 for EOF, -1 for error
    ssize_t readFileData(uint8_t* buffer, size_t size);

    // Check if all files have been extracted from the packed stream.
    bool isFinished() const;

    // Reset all internal state (both pack and unpack sides).
    void reset();

private:
    // Internal buffer for accumulating raw packed data on the read side
    std::vector<uint8_t> buffer_;
    size_t bufferOffset_ = 0;

    // Current file state (unpack side)
    FileMetaData currentMeta_{};
    uint64_t currentFileBytesConsumed_ = 0;
    bool hasCurrentFile_ = false;

    // When true, no more file headers can be parsed from the buffer
    bool finished_ = false;
};

} // namespace backup
