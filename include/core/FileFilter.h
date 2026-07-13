#pragma once

#include "common/Types.h"

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace backup {

struct FilterOptions {
    std::vector<std::string> excludePatterns;
    std::vector<std::string> includeExtensions;
    uint64_t minFileSize = 0;
    uint64_t maxFileSize = std::numeric_limits<uint64_t>::max();
    bool includeDirectories = true;
    bool includeHidden = true;
};

class FileFilter {
public:
    FileFilter() = default;
    explicit FileFilter(FilterOptions options);

    bool shouldInclude(const FileMetaData& meta) const;

private:
    FilterOptions options_;
};

} // namespace backup
