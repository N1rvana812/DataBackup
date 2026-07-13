#include "core/BackupEngine.h"

#include "core/DirectoryTraverser.h"
#include "core/FileSystemReader.h"
#include "core/FileSystemWriter.h"

#include <filesystem>
#include <memory>
#include <system_error>
#include <utility>
#include <vector>

namespace backup {

BackupEngine::BackupEngine(size_t chunkSize)
    : chunkSize_(chunkSize == 0 ? 4096 : chunkSize) {}

bool BackupEngine::backupDirectory(const std::filesystem::path& sourceRoot,
                                   const std::filesystem::path& archivePath,
                                   IArchiveWriter& archiveWriter,
                                   const BackupConfig& config,
                                   const FilterOptions& filterOptions) {
    stats_ = {};
    lastError_.clear();

    const std::filesystem::path tempArchivePath = archivePath.string() + ".tmp";
    std::error_code ec;
    std::filesystem::remove(tempArchivePath, ec);

    if (!archivePath.parent_path().empty()) {
        std::filesystem::create_directories(archivePath.parent_path(), ec);
        if (ec) {
            lastError_ = "failed to create archive parent directory: " + ec.message();
            return false;
        }
    }

    if (!archiveWriter.init(tempArchivePath.string(), config)) {
        lastError_ = "failed to initialize archive writer";
        return false;
    }

    DirectoryTraverser traverser(sourceRoot, FileFilter(filterOptions));
    const auto entries = traverser.scan();
    if (!traverser.errors().empty()) {
        lastError_ = traverser.errors().front();
        return false;
    }

    for (const auto& entry : entries) {
        auto reader = std::make_shared<FileSystemReader>(sourceRoot);
        if (!reader->open(entry.relativePath)) {
            lastError_ = "failed to open source entry: " + entry.relativePath;
            std::filesystem::remove(tempArchivePath, ec);
            return false;
        }

        if (!archiveWriter.addFile(reader)) {
            lastError_ = "failed to add entry to archive: " + entry.relativePath;
            std::filesystem::remove(tempArchivePath, ec);
            return false;
        }

        if (entry.isDirectory) {
            ++stats_.directoriesProcessed;
        } else {
            ++stats_.filesProcessed;
            stats_.bytesProcessed += entry.fileSize;
        }
    }

    if (!archiveWriter.finalize()) {
        lastError_ = "failed to finalize archive";
        std::filesystem::remove(tempArchivePath, ec);
        return false;
    }

    std::filesystem::rename(tempArchivePath, archivePath, ec);
    if (ec) {
        lastError_ = "failed to rename temporary archive: " + ec.message();
        std::filesystem::remove(tempArchivePath, ec);
        return false;
    }

    return true;
}

bool BackupEngine::restoreArchive(const std::filesystem::path& archivePath,
                                  IArchiveReader& archiveReader,
                                  const std::filesystem::path& targetRoot) {
    stats_ = {};
    lastError_.clear();

    if (!archiveReader.open(archivePath.string())) {
        lastError_ = "failed to open archive reader";
        return false;
    }

    FileSystemWriter writer(targetRoot);
    std::vector<uint8_t> buffer(chunkSize_);

    FileMetaData meta {};
    while (archiveReader.getNextFileMeta(meta)) {
        if (meta.isDirectory) {
            if (!writer.applyMetaData(meta)) {
                lastError_ = "failed to restore directory metadata: " + meta.relativePath;
                archiveReader.close();
                return false;
            }
            ++stats_.directoriesProcessed;
            continue;
        }

        if (!writer.open(meta.relativePath)) {
            lastError_ = "failed to open target file: " + meta.relativePath;
            archiveReader.close();
            return false;
        }

        uint64_t restoredSize = 0;
        while (true) {
            const ssize_t bytesRead = archiveReader.readChunk(buffer.data(), buffer.size());
            if (bytesRead < 0) {
                writer.close();
                archiveReader.close();
                lastError_ = "failed to read archived data: " + meta.relativePath;
                return false;
            }
            if (bytesRead == 0) {
                break;
            }
            if (!writer.writeChunk(buffer.data(), static_cast<size_t>(bytesRead))) {
                writer.close();
                archiveReader.close();
                lastError_ = "failed to write target file: " + meta.relativePath;
                return false;
            }
            restoredSize += static_cast<uint64_t>(bytesRead);
        }

        writer.close();
        if (!writer.applyMetaData(meta)) {
            archiveReader.close();
            lastError_ = "failed to restore file metadata: " + meta.relativePath;
            return false;
        }

        ++stats_.filesProcessed;
        stats_.bytesProcessed += restoredSize;
    }

    archiveReader.close();
    return true;
}

void BackupEngine::setIncrementalHandler(IncrementalHandler handler) {
    incrementalHandler_ = std::move(handler);
}

void BackupEngine::onFileEvent(const FileEvent& event) {
    if (incrementalHandler_) {
        incrementalHandler_(event);
    }
}

const EngineStats& BackupEngine::stats() const {
    return stats_;
}

const std::string& BackupEngine::lastError() const {
    return lastError_;
}

} // namespace backup
