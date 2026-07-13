#pragma once

#include "core/IFileWriter.h"

#include <filesystem>

namespace backup {

class FileSystemWriter : public IFileWriter {
public:
    explicit FileSystemWriter(std::filesystem::path targetRoot);
    ~FileSystemWriter() override;

    bool open(const std::string& relativePath) override;
    bool writeChunk(const uint8_t* buffer, size_t size) override;
    bool applyMetaData(const FileMetaData& meta) override;
    void close() override;

private:
    std::filesystem::path targetRoot_;
    int fd_ = -1;
};

} // namespace backup
