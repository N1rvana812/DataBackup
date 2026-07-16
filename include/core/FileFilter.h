#pragma once

#include "common/Types.h"

#include <cstdint>
#include <ctime>
#include <limits>
#include <string>
#include <vector>
#include <sys/types.h>

namespace backup {

struct FilterOptions {
    std::vector<std::string> excludePatterns;
    std::vector<std::string> includeExtensions;
    uint64_t minFileSize = 0;
    uint64_t maxFileSize = std::numeric_limits<uint64_t>::max();
    bool hasMinModifyTime = false;
    bool hasMaxModifyTime = false;
    time_t minModifyTime = 0;
    time_t maxModifyTime = 0;
    bool hasOwnerId = false;
    uid_t ownerId = 0;
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
