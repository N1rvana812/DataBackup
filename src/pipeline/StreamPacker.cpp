#include "pipeline/StreamPacker.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace backup {

// ============================================================================
// Write side (pack)
// ============================================================================

std::vector<uint8_t> StreamPacker::packHeader(const FileMetaData& meta) {
    FileEntryHeader header{};
    ArchiveFormat::metaDataToFileEntryHeader(meta, header);

    const size_t totalSize = sizeof(FileEntryHeader) + header.pathLength;
    std::vector<uint8_t> output(totalSize);

    // Copy fixed-size header
    std::memcpy(output.data(), &header, sizeof(FileEntryHeader));

    // Copy variable-length path
    if (header.pathLength > 0) {
        std::memcpy(output.data() + sizeof(FileEntryHeader),
                     meta.relativePath.data(), header.pathLength);
    }

    return output;
}

std::vector<uint8_t> StreamPacker::packData(const uint8_t* data, size_t size) {
    if (data == nullptr || size == 0) {
        return {};
    }
    return std::vector<uint8_t>(data, data + size);
}

std::vector<uint8_t> StreamPacker::endPack() {
    return {};
}

// ============================================================================
// Read side (unpack)
// ============================================================================

void StreamPacker::feedData(const uint8_t* data, size_t size) {
    if (data == nullptr || size == 0) {
        return;
    }
    // Prune already-consumed bytes from the front of the buffer to limit memory growth
    if (bufferOffset_ > 0 && bufferOffset_ >= buffer_.size() / 2) {
        const size_t remaining = buffer_.size() - bufferOffset_;
        if (remaining > 0) {
            std::memmove(buffer_.data(), buffer_.data() + bufferOffset_, remaining);
        }
        buffer_.resize(remaining);
        bufferOffset_ = 0;
    }

    buffer_.insert(buffer_.end(), data, data + size);
}

bool StreamPacker::tryReadFileMeta(FileMetaData& meta) {
    if (finished_) {
        return false;
    }

    // Need at least a full FileEntryHeader
    const size_t available = buffer_.size() - bufferOffset_;
    if (available < sizeof(FileEntryHeader)) {
        return false;
    }

    // Read the FileEntryHeader
    FileEntryHeader entryHeader{};
    std::memcpy(&entryHeader, buffer_.data() + bufferOffset_, sizeof(FileEntryHeader));

    // Validate pathLength — must be 1..4096
    if (entryHeader.pathLength == 0 || entryHeader.pathLength > 4096) {
        // Invalid header encountered — this is a corrupted entry, not end-of-stream.
        // Skip past it to avoid blocking subsequent valid entries.
        // Advance past the entire FileEntryHeader so the caller can try again.
        bufferOffset_ += sizeof(FileEntryHeader);
        return false;
    }

    // Check that we have the complete entry header + path buffered
    const size_t entryHeadSize = sizeof(FileEntryHeader) + entryHeader.pathLength;
    if (available < entryHeadSize) {
        return false;  // Need more data
    }

    // Extract path
    std::string path(entryHeader.pathLength, '\0');
    std::memcpy(&path[0],
                 buffer_.data() + bufferOffset_ + sizeof(FileEntryHeader),
                 entryHeader.pathLength);

    // Convert to FileMetaData
    ArchiveFormat::fileEntryHeaderToMetaData(entryHeader, path, meta);

    // Advance buffer offset past header + path
    bufferOffset_ += entryHeadSize;

    // Set current file state
    currentMeta_ = meta;
    currentFileBytesConsumed_ = 0;
    hasCurrentFile_ = true;

    return true;
}

ssize_t StreamPacker::readFileData(uint8_t* buffer, size_t size) {
    // No current file at all — error (readFileData called before tryReadFileMeta)
    if (!hasCurrentFile_ && currentMeta_.relativePath.empty()) {
        return -1;
    }
    // Current file already fully consumed — normal EOF
    if (!hasCurrentFile_) {
        return 0;
    }
    if (buffer == nullptr && size > 0) {
        return -1;
    }
    if (size == 0) {
        return 0;
    }

    // Directory has no data
    if (currentMeta_.isDirectory) {
        hasCurrentFile_ = false;
        return 0;
    }

    const uint64_t remaining = currentMeta_.fileSize - currentFileBytesConsumed_;
    if (remaining == 0) {
        hasCurrentFile_ = false;
        return 0;
    }

    const size_t available = buffer_.size() - bufferOffset_;
    if (available == 0) {
        return 0;  // Buffer empty — caller should feedData() and retry
    }

    const size_t toCopy = std::min({static_cast<size_t>(remaining), size, available});
    std::memcpy(buffer, buffer_.data() + bufferOffset_, toCopy);

    bufferOffset_ += toCopy;
    currentFileBytesConsumed_ += static_cast<uint64_t>(toCopy);

    if (currentFileBytesConsumed_ >= currentMeta_.fileSize) {
        hasCurrentFile_ = false;
    }

    return static_cast<ssize_t>(toCopy);
}

bool StreamPacker::isFinished() const {
    return finished_;
}

void StreamPacker::reset() {
    buffer_.clear();
    bufferOffset_ = 0;
    currentMeta_ = FileMetaData{};
    currentFileBytesConsumed_ = 0;
    hasCurrentFile_ = false;
    finished_ = false;
}

} // namespace backup
