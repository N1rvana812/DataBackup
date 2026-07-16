#include "core/DirectoryTraverser.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <system_error>

#include "common/PlatformCompat.h"

#if defined(_WIN32)
#include <io.h>
#else
#include <sys/stat.h>
#endif

namespace backup {
namespace {

bool fillMetaData(const std::filesystem::path& absolutePath,
                  const std::filesystem::path& relativePath,
                  FileMetaData& meta,
                  std::string& error) {
#if defined(_WIN32)
    struct _stat64 st {};
    const std::string widePath = absolutePath.string();
    if (::_stat64(widePath.c_str(), &st) != 0) {
#else
    struct stat st {};
    if (::lstat(absolutePath.c_str(), &st) != 0) {
#endif
        error = absolutePath.string() + ": " + std::strerror(errno);
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

DirectoryTraverser::DirectoryTraverser(std::filesystem::path rootPath, FileFilter filter)
    : rootPath_(std::move(rootPath)), filter_(std::move(filter)) {}

std::vector<FileMetaData> DirectoryTraverser::scan() {
    errors_.clear();
    std::vector<FileMetaData> result;

    std::error_code ec;
    const auto absoluteRoot = std::filesystem::absolute(rootPath_, ec);
    if (ec) {
        errors_.push_back("absolute(" + rootPath_.string() + "): " + ec.message());
        return result;
    }

    if (!std::filesystem::exists(absoluteRoot, ec) || ec) {
        errors_.push_back("source path does not exist: " + absoluteRoot.string());
        return result;
    }

    if (!std::filesystem::is_directory(absoluteRoot, ec) || ec) {
        errors_.push_back("source path is not a directory: " + absoluteRoot.string());
        return result;
    }

    const auto options = std::filesystem::directory_options::skip_permission_denied;
    std::filesystem::recursive_directory_iterator it(absoluteRoot, options, ec);
    std::filesystem::recursive_directory_iterator end;
    if (ec) {
        errors_.push_back("scan(" + absoluteRoot.string() + "): " + ec.message());
        return result;
    }

    while (it != end) {
        const auto currentPath = it->path();

        if (it->is_symlink(ec)) {
            if (it->is_directory(ec)) {
                it.disable_recursion_pending();
            }
            it.increment(ec);
            if (ec) {
                errors_.push_back("scan next: " + ec.message());
                ec.clear();
            }
            continue;
        }

        const auto relativePath = std::filesystem::relative(currentPath, absoluteRoot, ec);
        if (ec) {
            errors_.push_back("relative(" + currentPath.string() + "): " + ec.message());
            ec.clear();
            it.increment(ec);
            continue;
        }

        FileMetaData meta {};
        std::string error;
        if (fillMetaData(currentPath, relativePath, meta, error)) {
            if (filter_.shouldInclude(meta)) {
                result.push_back(meta);
            }
        } else {
            errors_.push_back(error);
        }

        it.increment(ec);
        if (ec) {
            errors_.push_back("scan next: " + ec.message());
            ec.clear();
        }
    }

    std::sort(result.begin(), result.end(), [](const FileMetaData& lhs, const FileMetaData& rhs) {
        return lhs.relativePath < rhs.relativePath;
    });

    return result;
}

const std::vector<std::string>& DirectoryTraverser::errors() const {
    return errors_;
}

} // namespace backup
