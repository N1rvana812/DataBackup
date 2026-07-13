#pragma once

#include "common/Types.h"
#include "core/FileFilter.h"

#include <filesystem>
#include <string>
#include <vector>

namespace backup {

class DirectoryTraverser {
public:
    DirectoryTraverser(std::filesystem::path rootPath, FileFilter filter = FileFilter());

    std::vector<FileMetaData> scan();
    const std::vector<std::string>& errors() const;

private:
    std::filesystem::path rootPath_;
    FileFilter filter_;
    std::vector<std::string> errors_;
};

} // namespace backup
