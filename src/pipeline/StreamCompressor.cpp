#include "pipeline/StreamCompressor.h"

#include <zlib.h>

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace backup {

StreamCompressor::StreamCompressor(int level)
    : compressionLevel_(std::clamp(level, 1, 9)) {}

StreamCompressor::~StreamCompressor() = default;

StreamCompressor::StreamCompressor(StreamCompressor&&) noexcept = default;
StreamCompressor& StreamCompressor::operator=(StreamCompressor&&) noexcept = default;

std::vector<uint8_t> StreamCompressor::compress(const uint8_t* data, size_t size) {
    if (data == nullptr && size > 0) {
        return {};
    }
    if (size == 0) {
        return {};
    }

    z_stream strm {};
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;

    // Initialize deflate with the configured compression level
    // Using deflateInit2 with MAX_WBITS + 16 for gzip-compatible format, but
    // we use raw deflate (-MAX_WBITS) since we manage the format ourselves.
    int rc = deflateInit2(&strm, compressionLevel_, Z_DEFLATED,
                          -MAX_WBITS,  // raw deflate, no zlib/gzip header
                          MAX_MEM_LEVEL,
                          Z_DEFAULT_STRATEGY);
    if (rc != Z_OK) {
        throw std::runtime_error("StreamCompressor: deflateInit2 failed");
    }

    strm.next_in = const_cast<Bytef*>(data);
    strm.avail_in = static_cast<uInt>(size);

    // Allocate output buffer — deflateBound gives the worst-case size
    const auto bound = deflateBound(&strm, static_cast<uLong>(size));
    std::vector<uint8_t> output(bound);

    strm.next_out = output.data();
    strm.avail_out = static_cast<uInt>(output.size());

    // Z_FINISH: this is a one-shot compression of a single chunk
    rc = deflate(&strm, Z_FINISH);
    if (rc != Z_STREAM_END) {
        deflateEnd(&strm);
        throw std::runtime_error("StreamCompressor: deflate did not finish properly");
    }

    const size_t compressedSize = strm.total_out;
    deflateEnd(&strm);

    output.resize(compressedSize);
    return output;
}

std::vector<uint8_t> StreamCompressor::decompress(const uint8_t* data,
                                                    size_t size,
                                                    size_t expectedOutSize) {
    if (data == nullptr || size == 0) {
        return {};
    }

    z_stream strm {};
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;

    // Raw inflate (matching the raw deflate above)
    int rc = inflateInit2(&strm, -MAX_WBITS);
    if (rc != Z_OK) {
        throw std::runtime_error("StreamCompressor: inflateInit2 failed");
    }

    strm.next_in = const_cast<Bytef*>(data);
    strm.avail_in = static_cast<uInt>(size);

    // Allocate output buffer. Start with the expected size, expand if needed.
    std::vector<uint8_t> output;
    const size_t initialSize = (expectedOutSize > 0) ? expectedOutSize : (size * 2);
    output.resize(initialSize);

    strm.next_out = output.data();
    strm.avail_out = static_cast<uInt>(output.size());

    // Loop in case the output buffer needs to grow
    for (;;) {
        rc = inflate(&strm, Z_FINISH);

        if (rc == Z_STREAM_END) {
            break;  // success
        }

        if (rc == Z_BUF_ERROR) {
            // Output buffer too small, expand it
            const size_t currentSize = output.size();
            const size_t newSize = currentSize * 2;
            output.resize(newSize);

            // Adjust next_out/avail_out for the expanded buffer
            // The position within the buffer is (total_out from the original pointer)
            strm.next_out = output.data() + strm.total_out;
            strm.avail_out = static_cast<uInt>(newSize - strm.total_out);
            continue;
        }

        // Any other return code is an error
        inflateEnd(&strm);
        throw std::runtime_error("StreamCompressor: inflate failed with code " +
                                 std::to_string(rc));
    }

    output.resize(strm.total_out);
    inflateEnd(&strm);
    return output;
}

} // namespace backup
