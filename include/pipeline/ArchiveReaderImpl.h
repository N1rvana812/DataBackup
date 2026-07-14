#pragma once

#include "pipeline/IArchiveReader.h"
#include "pipeline/ArchiveFormat.h"
#include "pipeline/StreamCompressor.h"
#include "pipeline/StreamEncryptor.h"
#include "pipeline/StreamPacker.h"

#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace backup {

class ArchiveReaderImpl : public IArchiveReader {
public:
    ArchiveReaderImpl();
    ~ArchiveReaderImpl() override;

    ArchiveReaderImpl(const ArchiveReaderImpl&) = delete;
    ArchiveReaderImpl& operator=(const ArchiveReaderImpl&) = delete;

    bool open(const std::string& archivePath) override;
    bool open(const std::string& archivePath, const BackupConfig& config) override;
    bool getNextFileMeta(FileMetaData& meta) override;
    ssize_t readChunk(uint8_t* buffer, size_t size) override;
    void close() override;

private:
    // Read raw bytes from the archive
    bool readRaw(void* buffer, size_t size);

    // Parse the global header and initialize decryptor/decompressor
    bool parseGlobalHeader(const std::string& password);

    // Read the next internal chunk (encrypted → decrypt → decompress)
    // and buffer the result for readChunk() to consume
    bool fillInternalBuffer();

    // Clean up resources
    void cleanup();

    FILE* file_ = nullptr;

    // Archive config parsed from header
    bool compressionEnabled_ = false;
    bool encryptionEnabled_ = false;
    bool packingEnabled_ = false;

    std::unique_ptr<StreamCompressor> decompressor_;
    std::unique_ptr<StreamEncryptor> decryptor_;
    std::unique_ptr<StreamPacker> packer_;

    std::vector<uint8_t> salt_;
    std::vector<uint8_t> iv_;
    std::vector<uint8_t> derivedKey_;

    // Current file state
    FileMetaData currentMeta_{};
    uint64_t currentFileRemaining_ = 0;  // bytes remaining in current file
    bool hasCurrentFile_ = false;

    // Internal decompressed data buffer for readChunk
    std::vector<uint8_t> internalBuffer_;
    size_t internalBufferOffset_ = 0;  // read position within internalBuffer_

    // File counts
    uint32_t totalFileCount_ = 0;
    uint32_t filesRead_ = 0;

    static constexpr size_t CHUNK_SIZE = 4096;
};

} // namespace backup
