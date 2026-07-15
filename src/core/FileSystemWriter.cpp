#include "core/FileSystemWriter.h"

#include <cerrno>
#include <cstring>
#include <iostream>
#include <system_error>

#include "common/PlatformCompat.h"

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/utime.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <utime.h>
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

bool ensureParentDirectory(const std::filesystem::path& fullPath) {
    std::error_code ec;
    const auto parent = fullPath.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, ec);
        if (ec) {
            return false;
        }
    }
    return true;
}

void warnMetaDataFailure(const std::string& operation,
                         const std::filesystem::path& path,
                         int errorNumber) {
    std::cerr << "[WARN] " << operation << " failed for " << path.string()
              << ": " << std::strerror(errorNumber) << '\n';
}

} // namespace

FileSystemWriter::FileSystemWriter(std::filesystem::path targetRoot)
    : targetRoot_(std::filesystem::absolute(std::move(targetRoot))) {}

FileSystemWriter::~FileSystemWriter() {
    close();
}

bool FileSystemWriter::open(const std::string& relativePath) {
    close();

    std::filesystem::path normalized;
    if (!normalizeRelativePath(relativePath, normalized)) {
        return false;
    }

    const auto fullPath = targetRoot_ / normalized;
    if (!ensureParentDirectory(fullPath)) {
        return false;
    }

#if defined(_WIN32)
    const std::string fullPathString = fullPath.string();
    fd_ = ::_open(fullPathString.c_str(), _O_WRONLY | _O_CREAT | _O_TRUNC | _O_BINARY, 0600);
#else
    fd_ = ::open(fullPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
#endif
    return fd_ >= 0;
}

bool FileSystemWriter::writeChunk(const uint8_t* buffer, size_t size) {
    if (fd_ < 0 || (buffer == nullptr && size > 0)) {
        return false;
    }

    size_t written = 0;
    while (written < size) {
#if defined(_WIN32)
        ssize_t result = ::_write(fd_, buffer + written, static_cast<unsigned int>(size - written));
#else
        ssize_t result = ::write(fd_, buffer + written, size - written);
#endif
        if (result < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        if (result == 0) {
            return false;
        }
        written += static_cast<size_t>(result);
    }

    return true;
}

bool FileSystemWriter::applyMetaData(const FileMetaData& meta) {
    std::filesystem::path normalized;
    if (!normalizeRelativePath(meta.relativePath, normalized)) {
        return false;
    }

    const auto fullPath = targetRoot_ / normalized;
    std::error_code ec;
    if (meta.isDirectory) {
        std::filesystem::create_directories(fullPath, ec);
    } else {
        std::filesystem::create_directories(fullPath.parent_path(), ec);
    }
    if (ec) {
        return false;
    }

    bool ok = true;
    const mode_t permissions = meta.permissions & 07777;
#if defined(_WIN32)
    const std::string fullPathString = fullPath.string();
    if (::_chmod(fullPathString.c_str(), permissions) != 0) {
#else
    if (::chmod(fullPath.c_str(), permissions) != 0) {
#endif
        warnMetaDataFailure("chmod", fullPath, errno);
        ok = false;
    }

#if defined(_WIN32)
    if (false) {
#else
    if (::chown(fullPath.c_str(), meta.ownerId, meta.groupId) != 0) {
#endif
        const int errorNumber = errno;
        warnMetaDataFailure("chown", fullPath, errorNumber);
        if (errorNumber != EPERM) {
            ok = false;
        }
    }

#if defined(_WIN32)
    struct _utimbuf times {};
    times.actime = static_cast<long>(meta.accessTime);
    times.modtime = static_cast<long>(meta.modifyTime);
    if (::_utime(fullPathString.c_str(), &times) != 0) {
#else
    struct utimbuf times {};
    times.actime = meta.accessTime;
    times.modtime = meta.modifyTime;
    if (::utime(fullPath.c_str(), &times) != 0) {
#endif
        warnMetaDataFailure("utime", fullPath, errno);
        ok = false;
    }

    return ok;
}

void FileSystemWriter::close() {
    if (fd_ >= 0) {
#if defined(_WIN32)
        ::_close(fd_);
#else
        ::close(fd_);
#endif
        fd_ = -1;
    }
}

} // namespace backup
