#include "core/FileSystemReader.h"

#include <cerrno>
#include <cstring>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace backup {
namespace {

bool normalizeRelativePath(const std::string& relativePath, std::filesystem::path& normalized) {
    std::filesystem::path input(relativePath);
    if (input.empty() || input.is_absolute()) {
        return false;
    }

    normalized = input.lexically_normal();
    for (const auto& component : normalized) {
        if (component == "..") {
            return false;
        }
    }

    return normalized != ".";
}

bool readMetaData(const std::filesystem::path& fullPath,
                  const std::filesystem::path& relativePath,
                  FileMetaData& meta) {
    struct stat st {};
    if (::lstat(fullPath.c_str(), &st) != 0) {
        return false;
    }

    meta.relativePath = relativePath.generic_string();
    meta.permissions = st.st_mode;
    meta.ownerId = st.st_uid;
    meta.groupId = st.st_gid;
    meta.accessTime = st.st_atime;
    meta.modifyTime = st.st_mtime;
    meta.isDirectory = S_ISDIR(st.st_mode);
    meta.fileSize = S_ISREG(st.st_mode) ? static_cast<uint64_t>(st.st_size) : 0;
    return true;
}

} // namespace

FileSystemReader::FileSystemReader(std::filesystem::path sourceRoot)
    : sourceRoot_(std::filesystem::absolute(std::move(sourceRoot))) {}

FileSystemReader::~FileSystemReader() {
    close();
}

bool FileSystemReader::open(const std::string& relativePath) {
    close();

    std::filesystem::path normalized;
    if (!normalizeRelativePath(relativePath, normalized)) {
        return false;
    }

    const auto fullPath = sourceRoot_ / normalized;
    if (!readMetaData(fullPath, normalized, metadata_)) {
        return false;
    }

    if (metadata_.isDirectory) {
        isOpen_ = true;
        return true;
    }

    if (!S_ISREG(metadata_.permissions)) {
        return false;
    }

    fd_ = ::open(fullPath.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd_ < 0) {
        return false;
    }

    isOpen_ = true;
    return true;
}

ssize_t FileSystemReader::readChunk(uint8_t* buffer, size_t size) {
    if (!isOpen_ || buffer == nullptr) {
        return -1;
    }

    if (metadata_.isDirectory || size == 0) {
        return 0;
    }

    ssize_t bytesRead = 0;
    do {
        bytesRead = ::read(fd_, buffer, size);
    } while (bytesRead < 0 && errno == EINTR);

    return bytesRead;
}

FileMetaData FileSystemReader::getMetaData() const {
    return metadata_;
}

void FileSystemReader::close() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
    isOpen_ = false;
}

} // namespace backup
