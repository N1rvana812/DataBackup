#include "core/FileFilter.h"

#include <filesystem>
#include <regex>
#include <utility>

#if defined(_WIN32)
#include <cwchar>
#else
#include <fnmatch.h>
#endif

namespace backup {
namespace {

bool matchesPattern(const std::string& value, const std::string& pattern) {
#if defined(_WIN32)
    const std::wstring wideValue(value.begin(), value.end());
    const std::wstring widePattern(pattern.begin(), pattern.end());
    if (wideValue == widePattern) {
        return true;
    }
    if (pattern.find('*') == std::string::npos && pattern.find('?') == std::string::npos) {
        return false;
    }
    std::wstring regexPattern = L"^";
    for (wchar_t ch : widePattern) {
        if (ch == L'*') {
            regexPattern += L".*";
        } else if (ch == L'?') {
            regexPattern += L".";
        } else if (ch == L'.') {
            regexPattern += L"\\.";
        } else {
            regexPattern += ch;
        }
    }
    regexPattern += L"$";
    std::wregex regex(regexPattern);
    return std::regex_match(wideValue, regex);
#else
    return fnmatch(pattern.c_str(), value.c_str(), FNM_PATHNAME) == 0 ||
           fnmatch(pattern.c_str(), value.c_str(), 0) == 0;
#endif
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

        if (options_.hasMinModifyTime && meta.modifyTime < options_.minModifyTime) {
            return false;
        }

        if (options_.hasMaxModifyTime && meta.modifyTime > options_.maxModifyTime) {
            return false;
        }

        if (options_.hasOwnerId && meta.ownerId != options_.ownerId) {
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
