#include "core/FileSystemReader.h"

#include <cerrno>
#include <cstring>

#include "common/PlatformCompat.h"

#if defined(_WIN32)
#include <io.h>
#include <fcntl.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

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
#if defined(_WIN32)
    struct _stat64 st {};
    const std::string fullPathString = fullPath.string();
    if (::_stat64(fullPathString.c_str(), &st) != 0) {
#else
    struct stat st {};
    if (::lstat(fullPath.c_str(), &st) != 0) {
#endif
        return false;
    }

    meta.relativePath = relativePath.generic_string();
    meta.permissions = static_cast<mode_t>(st.st_mode);
    meta.ownerId = static_cast<uid_t>(st.st_uid);
    meta.groupId = static_cast<gid_t>(st.st_gid);
    meta.accessTime = st.st_atime;
    meta.modifyTime = st.st_mtime;
#if defined(_WIN32)
    meta.isDirectory = (st.st_mode & _S_IFDIR) != 0;
    meta.fileSize = (st.st_mode & _S_IFREG) != 0 ? static_cast<uint64_t>(st.st_size) : 0;
#else
    meta.isDirectory = S_ISDIR(st.st_mode);
    meta.fileSize = S_ISREG(st.st_mode) ? static_cast<uint64_t>(st.st_size) : 0;
#endif
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

#if defined(_WIN32)
    if ((metadata_.permissions & _S_IFREG) == 0) {
#else
    if (!S_ISREG(metadata_.permissions)) {
#endif
        return false;
    }

#if defined(_WIN32)
    const std::string fullPathString = fullPath.string();
    fd_ = ::_open(fullPathString.c_str(), _O_RDONLY | _O_BINARY);
#else
    fd_ = ::open(fullPath.c_str(), O_RDONLY | O_CLOEXEC);
#endif
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
#if defined(_WIN32)
        bytesRead = ::_read(fd_, buffer, static_cast<unsigned int>(size));
#else
        bytesRead = ::read(fd_, buffer, size);
#endif
    } while (bytesRead < 0 && errno == EINTR);

    return bytesRead;
}

FileMetaData FileSystemReader::getMetaData() const {
    return metadata_;
}

void FileSystemReader::close() {
    if (fd_ >= 0) {
#if defined(_WIN32)
        ::_close(fd_);
#else
        ::close(fd_);
#endif
        fd_ = -1;
    }
    isOpen_ = false;
}

} // namespace backup
