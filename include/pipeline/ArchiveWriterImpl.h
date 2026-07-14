#pragma once

#include "pipeline/IArchiveWriter.h"
#include "pipeline/ArchiveFormat.h"
#include "pipeline/StreamCompressor.h"
#include "pipeline/StreamEncryptor.h"

#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace backup {

class ArchiveWriterImpl : public IArchiveWriter {
public:
    ArchiveWriterImpl();
    ~ArchiveWriterImpl() override;

    ArchiveWriterImpl(const ArchiveWriterImpl&) = delete;
    ArchiveWriterImpl& operator=(const ArchiveWriterImpl&) = delete;

    bool init(const std::string& archivePath, const BackupConfig& config) override;
    bool addFile(std::shared_ptr<IFileReader> reader) override;
    bool finalize() override;

private:
    // Write raw bytes to the archive file
    bool writeRaw(const void* data, size_t size);

    // Write a file entry header + path to the archive
    bool writeFileHeader(const FileMetaData& meta);

    // Write directory entry (header only, no data)
    bool writeDirectoryEntry(const FileMetaData& meta);

    // Write regular file entry (header + chunked compressed+encrypted data)
    bool writeRegularFile(std::shared_ptr<IFileReader> reader);

    // Write the archive footer
    bool writeFooter();

    // Clean up on failure
    void cleanup();

    FILE* file_ = nullptr;
    BackupConfig config_;
    std::unique_ptr<StreamCompressor> compressor_;
    std::unique_ptr<StreamEncryptor> encryptor_;

    std::vector<uint8_t> salt_;
    std::vector<uint8_t> iv_;
    std::vector<uint8_t> derivedKey_;

    uint32_t fileCount_ = 0;
    uint64_t indexOffset_ = 0;

    static constexpr size_t CHUNK_SIZE = 4096;
};

} // namespace backup
