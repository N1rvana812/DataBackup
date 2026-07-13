#pragma once

#include "common/Types.h"
#include "core/FileFilter.h"
#include "monitor/IFileEventListener.h"
#include "pipeline/IArchiveReader.h"
#include "pipeline/IArchiveWriter.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

namespace backup {

struct EngineStats {
    uint64_t filesProcessed = 0;
    uint64_t directoriesProcessed = 0;
    uint64_t bytesProcessed = 0;
};

class BackupEngine : public IFileEventListener {
public:
    using IncrementalHandler = std::function<void(const FileEvent&)>;

    explicit BackupEngine(size_t chunkSize = 4096);

    bool backupDirectory(const std::filesystem::path& sourceRoot,
                         const std::filesystem::path& archivePath,
                         IArchiveWriter& archiveWriter,
                         const BackupConfig& config,
                         const FilterOptions& filterOptions = FilterOptions());

    bool restoreArchive(const std::filesystem::path& archivePath,
                        IArchiveReader& archiveReader,
                        const std::filesystem::path& targetRoot);

    void setIncrementalHandler(IncrementalHandler handler);
    void onFileEvent(const FileEvent& event) override;

    const EngineStats& stats() const;
    const std::string& lastError() const;

private:
    size_t chunkSize_;
    EngineStats stats_{};
    std::string lastError_;
    IncrementalHandler incrementalHandler_;
};

} // namespace backup
