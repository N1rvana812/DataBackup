#include "pipeline/StreamCompressor.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace backup {

// ============================================================================
// Simple Run-Length Encoding (PackBits-style)
// ============================================================================
//
// Compression format:
//   - Literal block: [0x00-0x7F][count-1][data bytes...]  (1-128 bytes)
//   - Run block:     [0x80-0xFF][count-1+128][value]       (1-128 repeats)
//
// The high bit of the control byte distinguishes runs from literals.
// Lower 7 bits encode count-1, allowing 1-128 items per block.
//
// This is a simple algorithm well-suited for data with repeated byte
// patterns. Incompressible data may expand by up to ~0.8% worst case
// (1 control byte per 128 literal bytes).
// ============================================================================

namespace {

// Encode a run of repeated bytes
void encodeRun(std::vector<uint8_t>& output, uint8_t value, size_t count) {
    while (count > 0) {
        const uint8_t n = static_cast<uint8_t>(std::min(count, size_t(128)) - 1);
        output.push_back(static_cast<uint8_t>(0x80 | n));
        output.push_back(value);
        count -= (n + 1);
    }
}

// Encode a literal block
void encodeLiterals(std::vector<uint8_t>& output, const uint8_t* data, size_t count) {
    while (count > 0) {
        const uint8_t n = static_cast<uint8_t>(std::min(count, size_t(128)) - 1);
        output.push_back(n);  // high bit clear = literal
        output.insert(output.end(), data, data + n + 1);
        data += (n + 1);
        count -= (n + 1);
    }
}

}  // namespace

StreamCompressor::StreamCompressor(int level)
    : compressionLevel_(std::clamp(level, 1, 9)) {}

StreamCompressor::~StreamCompressor() = default;

StreamCompressor::StreamCompressor(StreamCompressor&&) noexcept = default;
StreamCompressor& StreamCompressor::operator=(StreamCompressor&&) noexcept = default;

std::vector<uint8_t> StreamCompressor::compress(const uint8_t* data, size_t size) {
    if (data == nullptr || size == 0) {
        return {};
    }

    std::vector<uint8_t> output;
    output.reserve(size);  // optimistic: compressed ≤ original

    size_t pos = 0;
    while (pos < size) {
        // Count repeating bytes starting at pos
        size_t runLen = 1;
        while (pos + runLen < size && data[pos + runLen] == data[pos] && runLen < 128) {
            ++runLen;
        }

        if (runLen >= 3) {
            // Worth encoding as a run (saves at least 1 byte vs literal)
            encodeRun(output, data[pos], runLen);
            pos += runLen;
        } else {
            // Collect literals until we find a run worth encoding
            const size_t literalStart = pos;
            ++pos;
            while (pos < size) {
                // Peek ahead: would a run start at 'pos'?
                size_t peek = 1;
                while (pos + peek < size && data[pos + peek] == data[pos] && peek < 128) {
                    ++peek;
                }
                if (peek >= 3) {
                    break;  // stop literal block, next run is profitable
                }
                ++pos;
                if (pos - literalStart >= 128) {
                    break;  // max literal block size reached
                }
            }
            encodeLiterals(output, data + literalStart, pos - literalStart);
        }
    }

    return output;
}

std::vector<uint8_t> StreamCompressor::decompress(const uint8_t* data,
                                                    size_t size,
                                                    size_t /*expectedOutSize*/) {
    if (data == nullptr || size == 0) {
        return {};
    }

    std::vector<uint8_t> output;

    size_t pos = 0;
    while (pos < size) {
        const uint8_t control = data[pos++];

        if (pos >= size) {
            throw std::runtime_error("StreamCompressor: truncated RLE data");
        }

        const size_t count = (control & 0x7F) + 1;

        if (control & 0x80) {
            // Run block: next byte is the repeated value
            const uint8_t value = data[pos++];
            output.insert(output.end(), count, value);
        } else {
            // Literal block: next 'count' bytes are raw data
            if (pos + count > size) {
                throw std::runtime_error("StreamCompressor: truncated literal block");
            }
            output.insert(output.end(), data + pos, data + pos + count);
            pos += count;
        }
    }

    return output;
}

} // namespace backup
