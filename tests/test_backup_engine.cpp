/**
 * @file test_backup_engine.cpp
 * @brief Google Test end-to-end tests for BackupEngine
 *
 * Tests backup and restore orchestration with real filesystem trees and the
 * concrete archive reader/writer implementations.
 */

#include "core/BackupEngine.h"
#include "pipeline/ArchiveReaderImpl.h"
#include "pipeline/ArchiveWriterImpl.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include <unistd.h>

namespace backup {
namespace {

class TempDir {
public:
    explicit TempDir(const std::string& name) {
        path_ = std::filesystem::temp_directory_path() /
                ("databackup_" + name + "_" + std::to_string(::getpid()));
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
        std::filesystem::create_directories(path_);
    }

    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }

    const std::filesystem::path& path() const {
        return path_;
    }

private:
    std::filesystem::path path_;
};

void writeTextFile(const std::filesystem::path& path, const std::string& contents) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << contents;
}

std::string readTextFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

void createSampleTree(const std::filesystem::path& root) {
    std::filesystem::create_directories(root / "docs");
    writeTextFile(root / "hello.txt", "hello backup\n");
    writeTextFile(root / "docs" / "readme.md", "# Readme\n");
    writeTextFile(root / "docs" / "repeated.txt", "AAAAAAAAAAAAAAAAAAAAAAAAAAAA\n");
}

void expectSampleTreeRestored(const std::filesystem::path& restored) {
    EXPECT_EQ(readTextFile(restored / "hello.txt"), "hello backup\n");
    EXPECT_EQ(readTextFile(restored / "docs" / "readme.md"), "# Readme\n");
    EXPECT_EQ(readTextFile(restored / "docs" / "repeated.txt"),
              "AAAAAAAAAAAAAAAAAAAAAAAAAAAA\n");
}

TEST(BackupEngineTest, BackupAndRestoreDirectoryWithoutTransforms) {
    TempDir temp("engine_plain");
    const auto source = temp.path() / "source";
    const auto archive = temp.path() / "archive.dbak";
    const auto restored = temp.path() / "restored";
    createSampleTree(source);

    BackupConfig config;
    BackupEngine engine;
    ArchiveWriterImpl writer;
    ASSERT_TRUE(engine.backupDirectory(source, archive, writer, config))
        << engine.lastError();

    EXPECT_EQ(engine.stats().filesProcessed, 3);
    EXPECT_EQ(engine.stats().directoriesProcessed, 1);
    EXPECT_GT(engine.stats().bytesProcessed, 0);

    ArchiveReaderImpl reader;
    ASSERT_TRUE(engine.restoreArchive(archive, reader, restored, config))
        << engine.lastError();

    EXPECT_EQ(engine.stats().filesProcessed, 3);
    EXPECT_EQ(engine.stats().directoriesProcessed, 1);
    expectSampleTreeRestored(restored);
}

TEST(BackupEngineTest, BackupAndRestoreWithCompressionEncryptionAndPacking) {
    TempDir temp("engine_all_flags");
    const auto source = temp.path() / "source";
    const auto archive = temp.path() / "archive.dbak";
    const auto restored = temp.path() / "restored";
    createSampleTree(source);

    BackupConfig config;
    config.enableCompression = true;
    config.enableEncryption = true;
    config.enablePacking = true;
    config.password = "secret";

    BackupEngine engine;
    ArchiveWriterImpl writer;
    ASSERT_TRUE(engine.backupDirectory(source, archive, writer, config))
        << engine.lastError();

    ArchiveReaderImpl reader;
    ASSERT_TRUE(engine.restoreArchive(archive, reader, restored, config))
        << engine.lastError();

    EXPECT_EQ(engine.stats().filesProcessed, 3);
    EXPECT_EQ(engine.stats().directoriesProcessed, 1);
    expectSampleTreeRestored(restored);
}

TEST(BackupEngineTest, BackupRespectsFilterOptions) {
    TempDir temp("engine_filter");
    const auto source = temp.path() / "source";
    const auto archive = temp.path() / "filtered.dbak";
    const auto restored = temp.path() / "restored";
    writeTextFile(source / "keep.txt", "keep");
    writeTextFile(source / "skip.log", "skip");
    writeTextFile(source / ".hidden.txt", "hidden");
    writeTextFile(source / "nested" / "keep.md", "nested keep");

    FilterOptions filter;
    filter.includeDirectories = false;
    filter.includeHidden = false;
    filter.includeExtensions = {".txt", ".md"};
    filter.excludePatterns = {"skip*"};

    BackupConfig config;
    BackupEngine engine;
    ArchiveWriterImpl writer;
    ASSERT_TRUE(engine.backupDirectory(source, archive, writer, config, filter))
        << engine.lastError();

    EXPECT_EQ(engine.stats().filesProcessed, 2);
    EXPECT_EQ(engine.stats().directoriesProcessed, 0);

    ArchiveReaderImpl reader;
    ASSERT_TRUE(engine.restoreArchive(archive, reader, restored, config))
        << engine.lastError();

    EXPECT_EQ(readTextFile(restored / "keep.txt"), "keep");
    EXPECT_EQ(readTextFile(restored / "nested" / "keep.md"), "nested keep");
    EXPECT_FALSE(std::filesystem::exists(restored / "skip.log"));
    EXPECT_FALSE(std::filesystem::exists(restored / ".hidden.txt"));
}

TEST(BackupEngineTest, BackupFailsForMissingSource) {
    TempDir temp("engine_missing_source");
    const auto source = temp.path() / "missing";
    const auto archive = temp.path() / "archive.dbak";

    BackupConfig config;
    BackupEngine engine;
    ArchiveWriterImpl writer;

    EXPECT_FALSE(engine.backupDirectory(source, archive, writer, config));
    EXPECT_NE(engine.lastError().find("source path does not exist"), std::string::npos);
    EXPECT_FALSE(std::filesystem::exists(archive));
}

TEST(BackupEngineTest, IncrementalHandlerReceivesFileEvents) {
    BackupEngine engine;
    bool triggered = false;
    FileEvent received;

    engine.setIncrementalHandler([&](const FileEvent& event) {
        triggered = true;
        received = event;
    });

    FileEvent event;
    event.filePath = "/tmp/example.txt";
    event.type = FileEventType::CREATED;
    event.isDirectory = false;
    event.timestamp = 42;

    engine.onFileEvent(event);

    EXPECT_TRUE(triggered);
    EXPECT_EQ(received.filePath, "/tmp/example.txt");
    EXPECT_EQ(received.type, FileEventType::CREATED);
    EXPECT_FALSE(received.isDirectory);
}

}  // namespace
}  // namespace backup
