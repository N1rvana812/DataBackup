#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>

namespace backup {

// ============================================================================
// Streaming RLE Compressor / Decompressor
// ============================================================================
//
// Each 4KB chunk is independently compressed using PackBits-style RLE.
// This trades some compression ratio for true streaming (no need to hold
// the entire file in memory). The per-chunk approach also allows the reader
// to seek and decompress individual chunks without processing the entire file.
// ============================================================================

class StreamCompressor {
public:
    // @param level  Compression level 1-9 (kept for API compatibility; RLE ignores level)
    explicit StreamCompressor(int level = 6);
    ~StreamCompressor();

    StreamCompressor(const StreamCompressor&) = delete;
    StreamCompressor& operator=(const StreamCompressor&) = delete;
    StreamCompressor(StreamCompressor&&) noexcept;
    StreamCompressor& operator=(StreamCompressor&&) noexcept;

    // Compress a single chunk of data.
    // Each call is independent — deflateInit/deflate/deflateEnd per chunk.
    // @param data   Input data buffer
    // @param size   Input data size in bytes
    // @return       Compressed data (may be larger than input for
    //               incompressible data; the caller handles this)
    std::vector<uint8_t> compress(const uint8_t* data, size_t size);

    // Decompress a single chunk of data.
    // Each call is independent — inflateInit/inflate/inflateEnd per chunk.
    // @param data              Compressed data buffer
    // @param size              Compressed data size in bytes
    // @param expectedOutSize   Hint for the expected decompressed size
    // @return                  Decompressed data
    std::vector<uint8_t> decompress(const uint8_t* data, size_t size,
                                     size_t expectedOutSize = 4096);

private:
    int compressionLevel_;
};

} // namespace backup
