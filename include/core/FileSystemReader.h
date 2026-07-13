#pragma once

#include "core/IFileReader.h"

#include <filesystem>
#include <string>

namespace backup {

class FileSystemReader : public IFileReader {
public:
    explicit FileSystemReader(std::filesystem::path sourceRoot);
    ~FileSystemReader() override;

    bool open(const std::string& relativePath) override;
    ssize_t readChunk(uint8_t* buffer, size_t size) override;
    FileMetaData getMetaData() const override;
    void close() override;

private:
    std::filesystem::path sourceRoot_;
    FileMetaData metadata_{};
    int fd_ = -1;
    bool isOpen_ = false;
};

} // namespace backup
