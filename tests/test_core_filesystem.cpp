/**
 * @file test_core_filesystem.cpp
 * @brief Google Test integration tests for core filesystem components
 *
 * Tests DirectoryTraverser, FileSystemReader, and FileSystemWriter against
 * temporary real filesystem trees.
 */

#include "core/DirectoryTraverser.h"
#include "core/FileFilter.h"
#include "core/FileSystemReader.h"
#include "core/FileSystemWriter.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <sys/stat.h>
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

std::vector<std::string> sortedPaths(const std::vector<FileMetaData>& entries) {
    std::vector<std::string> paths;
    for (const auto& entry : entries) {
        paths.push_back(entry.relativePath);
    }
    std::sort(paths.begin(), paths.end());
    return paths;
}

TEST(DirectoryTraverserTest, ScanReturnsSortedFilesAndDirectories) {
    TempDir temp("traverser_scan");
    std::filesystem::create_directories(temp.path() / "docs");
    writeTextFile(temp.path() / "b.txt", "bbb");
    writeTextFile(temp.path() / "docs" / "a.txt", "aaaa");

    DirectoryTraverser traverser(temp.path());
    const auto entries = traverser.scan();

    EXPECT_TRUE(traverser.errors().empty());
    EXPECT_EQ(sortedPaths(entries),
              (std::vector<std::string>{"b.txt", "docs", "docs/a.txt"}));

    const auto fileIt = std::find_if(entries.begin(), entries.end(), [](const auto& entry) {
        return entry.relativePath == "docs/a.txt";
    });
    ASSERT_NE(fileIt, entries.end());
    EXPECT_FALSE(fileIt->isDirectory);
    EXPECT_EQ(fileIt->fileSize, 4);

    const auto dirIt = std::find_if(entries.begin(), entries.end(), [](const auto& entry) {
        return entry.relativePath == "docs";
    });
    ASSERT_NE(dirIt, entries.end());
    EXPECT_TRUE(dirIt->isDirectory);
}

TEST(DirectoryTraverserTest, AppliesFilterOptions) {
    TempDir temp("traverser_filter");
    writeTextFile(temp.path() / "keep.txt", "keep");
    writeTextFile(temp.path() / "skip.log", "skip");
    writeTextFile(temp.path() / ".hidden.txt", "hidden");
    writeTextFile(temp.path() / "nested" / "also_keep.txt", "nested");

    FilterOptions options;
    options.includeDirectories = false;
    options.includeHidden = false;
    options.includeExtensions = {".txt"};
    options.excludePatterns = {"skip*"};

    DirectoryTraverser traverser(temp.path(), FileFilter(options));
    const auto entries = traverser.scan();

    EXPECT_TRUE(traverser.errors().empty());
    EXPECT_EQ(sortedPaths(entries),
              (std::vector<std::string>{"keep.txt", "nested/also_keep.txt"}));
}

TEST(DirectoryTraverserTest, ReportsInvalidSourcePath) {
    TempDir temp("traverser_invalid");
    const auto missing = temp.path() / "missing";

    DirectoryTraverser traverser(missing);
    const auto entries = traverser.scan();

    EXPECT_TRUE(entries.empty());
    ASSERT_FALSE(traverser.errors().empty());
    EXPECT_NE(traverser.errors().front().find("does not exist"), std::string::npos);
}

TEST(FileSystemReaderTest, ReadsFileInChunksAndReturnsMetadata) {
    TempDir temp("reader_file");
    writeTextFile(temp.path() / "nested" / "file.txt", "abcdef");

    FileSystemReader reader(temp.path());
    ASSERT_TRUE(reader.open("nested/file.txt"));

    const auto meta = reader.getMetaData();
    EXPECT_EQ(meta.relativePath, "nested/file.txt");
    EXPECT_FALSE(meta.isDirectory);
    EXPECT_EQ(meta.fileSize, 6);

    uint8_t buffer[3] = {};
    std::string output;
    ssize_t bytesRead = 0;
    while ((bytesRead = reader.readChunk(buffer, sizeof(buffer))) > 0) {
        output.append(reinterpret_cast<char*>(buffer), static_cast<size_t>(bytesRead));
    }

    EXPECT_EQ(bytesRead, 0);
    EXPECT_EQ(output, "abcdef");
}

TEST(FileSystemReaderTest, RejectsUnsafeRelativePaths) {
    TempDir temp("reader_unsafe");
    writeTextFile(temp.path() / "file.txt", "data");

    FileSystemReader reader(temp.path());
    EXPECT_FALSE(reader.open("../file.txt"));
    EXPECT_FALSE(reader.open((temp.path() / "file.txt").string()));
}

TEST(FileSystemReaderTest, OpensDirectoryAndReadsEof) {
    TempDir temp("reader_directory");
    std::filesystem::create_directories(temp.path() / "subdir");

    FileSystemReader reader(temp.path());
    ASSERT_TRUE(reader.open("subdir"));

    uint8_t buffer[8] = {};
    EXPECT_EQ(reader.readChunk(buffer, sizeof(buffer)), 0);
    EXPECT_TRUE(reader.getMetaData().isDirectory);
}

TEST(FileSystemWriterTest, WritesChunksAndCreatesParentDirectories) {
    TempDir temp("writer_file");

    FileSystemWriter writer(temp.path());
    ASSERT_TRUE(writer.open("nested/output.txt"));
    EXPECT_TRUE(writer.writeChunk(reinterpret_cast<const uint8_t*>("abc"), 3));
    EXPECT_TRUE(writer.writeChunk(reinterpret_cast<const uint8_t*>("def"), 3));
    writer.close();

    EXPECT_EQ(readTextFile(temp.path() / "nested" / "output.txt"), "abcdef");
}

TEST(FileSystemWriterTest, RejectsUnsafeRelativePaths) {
    TempDir temp("writer_unsafe");
    FileSystemWriter writer(temp.path());

    EXPECT_FALSE(writer.open("../escape.txt"));
    EXPECT_FALSE(writer.open((temp.path() / "absolute.txt").string()));
}

TEST(FileSystemWriterTest, ApplyMetaDataCreatesDirectoryAndSetsMode) {
    TempDir temp("writer_metadata");

    FileMetaData meta{};
    meta.relativePath = "restored/dir";
    meta.permissions = S_IFDIR | 0750;
    meta.ownerId = static_cast<uid_t>(::getuid());
    meta.groupId = static_cast<gid_t>(::getgid());
    meta.accessTime = 100000;
    meta.modifyTime = 200000;
    meta.isDirectory = true;
    meta.fileSize = 0;

    FileSystemWriter writer(temp.path());
    ASSERT_TRUE(writer.applyMetaData(meta));

    const auto restored = temp.path() / "restored" / "dir";
    ASSERT_TRUE(std::filesystem::is_directory(restored));
    const auto mode = std::filesystem::status(restored).permissions();
    EXPECT_EQ((mode & std::filesystem::perms::owner_all),
              std::filesystem::perms::owner_all);
}

}  // namespace
}  // namespace backup
