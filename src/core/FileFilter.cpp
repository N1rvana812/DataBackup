#include "core/FileFilter.h"

#include <filesystem>
#include <fnmatch.h>
#include <utility>

namespace backup {
namespace {

bool matchesPattern(const std::string& value, const std::string& pattern) {
    return fnmatch(pattern.c_str(), value.c_str(), FNM_PATHNAME) == 0 ||
           fnmatch(pattern.c_str(), value.c_str(), 0) == 0;
}

bool hasHiddenComponent(const std::filesystem::path& path) {
    for (const auto& component : path) {
        const std::string text = component.string();
        if (text.size() > 1 && text.front() == '.') {
            return true;
        }
    }
    return false;
}

} // namespace

FileFilter::FileFilter(FilterOptions options)
    : options_(std::move(options)) {}

bool FileFilter::shouldInclude(const FileMetaData& meta) const {
    const std::filesystem::path relativePath(meta.relativePath);
    const std::string genericPath = relativePath.generic_string();
    const std::string filename = relativePath.filename().string();

    if (meta.isDirectory && !options_.includeDirectories) {
        return false;
    }

    if (!options_.includeHidden && hasHiddenComponent(relativePath)) {
        return false;
    }

    for (const auto& pattern : options_.excludePatterns) {
        if (matchesPattern(genericPath, pattern) || matchesPattern(filename, pattern)) {
            return false;
        }
    }

    if (!meta.isDirectory) {
        if (meta.fileSize < options_.minFileSize || meta.fileSize > options_.maxFileSize) {
            return false;
        }

        if (!options_.includeExtensions.empty()) {
            const std::string extension = relativePath.extension().string();
            bool matched = false;
            for (const auto& allowedExtension : options_.includeExtensions) {
                if (extension == allowedExtension) {
                    matched = true;
                    break;
                }
            }
            if (!matched) {
                return false;
            }
        }
    }

    return true;
}

} // namespace backup
