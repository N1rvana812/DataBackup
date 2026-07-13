#include "core/FileSystemWriter.h"

#include <cerrno>
#include <cstring>
#include <iostream>
#include <system_error>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <utime.h>

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

    fd_ = ::open(fullPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    return fd_ >= 0;
}

bool FileSystemWriter::writeChunk(const uint8_t* buffer, size_t size) {
    if (fd_ < 0 || (buffer == nullptr && size > 0)) {
        return false;
    }

    size_t written = 0;
    while (written < size) {
        ssize_t result = ::write(fd_, buffer + written, size - written);
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
    if (::chmod(fullPath.c_str(), permissions) != 0) {
        warnMetaDataFailure("chmod", fullPath, errno);
        ok = false;
    }

    if (::chown(fullPath.c_str(), meta.ownerId, meta.groupId) != 0) {
        const int errorNumber = errno;
        warnMetaDataFailure("chown", fullPath, errorNumber);
        if (errorNumber != EPERM) {
            ok = false;
        }
    }

    struct utimbuf times {};
    times.actime = meta.accessTime;
    times.modtime = meta.modifyTime;
    if (::utime(fullPath.c_str(), &times) != 0) {
        warnMetaDataFailure("utime", fullPath, errno);
        ok = false;
    }

    return ok;
}

void FileSystemWriter::close() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

} // namespace backup
