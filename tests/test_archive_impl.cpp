/**
 * @file test_archive_impl.cpp
 * @brief Google Test integration tests for archive reader/writer implementations
 *
 * Tests ArchiveWriterImpl and ArchiveReaderImpl using temporary archive files
 * and in-memory IFileReader implementations.
 */

#include "core/IFileReader.h"
#include "pipeline/ArchiveReaderImpl.h"
#include "pipeline/ArchiveWriterImpl.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <utility>
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

class MemoryFileReader : public IFileReader {
public:
    MemoryFileReader(std::string path, std::vector<uint8_t> data, bool isDirectory = false)
        : data_(std::move(data)) {
        meta_.relativePath = std::move(path);
        meta_.permissions = isDirectory ? (S_IFDIR | 0755) : (S_IFREG | 0644);
        meta_.ownerId = static_cast<uid_t>(::getuid());
        meta_.groupId = static_cast<gid_t>(::getgid());
        meta_.accessTime = 1000000;
        meta_.modifyTime = 2000000;
        meta_.isDirectory = isDirectory;
        meta_.fileSize = isDirectory ? 0 : data_.size();
    }

    bool open(const std::string& relativePath) override {
        offset_ = 0;
        return relativePath == meta_.relativePath;
    }

    ssize_t readChunk(uint8_t* buffer, size_t size) override {
        if (meta_.isDirectory) {
            return 0;
        }
        if (buffer == nullptr && size > 0) {
            return -1;
        }
        const size_t remaining = data_.size() - offset_;
        if (remaining == 0 || size == 0) {
            return 0;
        }
        const size_t toCopy = std::min(size, remaining);
        std::copy(data_.begin() + static_cast<std::ptrdiff_t>(offset_),
                  data_.begin() + static_cast<std::ptrdiff_t>(offset_ + toCopy),
                  buffer);
        offset_ += toCopy;
        return static_cast<ssize_t>(toCopy);
    }

    FileMetaData getMetaData() const override {
        return meta_;
    }

    void close() override {
        offset_ = 0;
    }

private:
    FileMetaData meta_{};
    std::vector<uint8_t> data_;
    size_t offset_ = 0;
};

std::shared_ptr<IFileReader> makeFileReader(const std::string& path,
                                            const std::string& contents) {
    return std::make_shared<MemoryFileReader>(
        path, std::vector<uint8_t>(contents.begin(), contents.end()), false);
}

std::shared_ptr<IFileReader> makeDirectoryReader(const std::string& path) {
    return std::make_shared<MemoryFileReader>(path, std::vector<uint8_t>{}, true);
}

std::string readCurrentFile(IArchiveReader& reader) {
    std::vector<uint8_t> buffer(5);
    std::string output;

    while (true) {
        const ssize_t bytesRead = reader.readChunk(buffer.data(), buffer.size());
        EXPECT_GE(bytesRead, 0);
        if (bytesRead <= 0) {
            break;
        }
        output.append(reinterpret_cast<char*>(buffer.data()),
                      static_cast<size_t>(bytesRead));
    }

    return output;
}

TEST(ArchiveImplTest, RoundtripsMultipleEntriesWithoutTransforms) {
    TempDir temp("archive_plain");
    const auto archivePath = temp.path() / "plain.dbak";

    BackupConfig config;
    ArchiveWriterImpl writer;
    ASSERT_TRUE(writer.init(archivePath.string(), config));
    ASSERT_TRUE(writer.addFile(makeDirectoryReader("docs")));
    ASSERT_TRUE(writer.addFile(makeFileReader("docs/readme.txt", "hello archive")));
    ASSERT_TRUE(writer.addFile(makeFileReader("empty.txt", "")));
    ASSERT_TRUE(writer.finalize());

    ArchiveReaderImpl reader;
    ASSERT_TRUE(reader.open(archivePath.string(), config));

    FileMetaData meta{};
    ASSERT_TRUE(reader.getNextFileMeta(meta));
    EXPECT_EQ(meta.relativePath, "docs");
    EXPECT_TRUE(meta.isDirectory);

    ASSERT_TRUE(reader.getNextFileMeta(meta));
    EXPECT_EQ(meta.relativePath, "docs/readme.txt");
    EXPECT_FALSE(meta.isDirectory);
    EXPECT_EQ(readCurrentFile(reader), "hello archive");

    ASSERT_TRUE(reader.getNextFileMeta(meta));
    EXPECT_EQ(meta.relativePath, "empty.txt");
    EXPECT_FALSE(meta.isDirectory);
    EXPECT_EQ(readCurrentFile(reader), "");

    EXPECT_FALSE(reader.getNextFileMeta(meta));
    reader.close();
}

TEST(ArchiveImplTest, RoundtripsWithCompressionEncryptionAndPacking) {
    TempDir temp("archive_all_flags");
    const auto archivePath = temp.path() / "packed.dbak";

    BackupConfig config;
    config.enableCompression = true;
    config.enableEncryption = true;
    config.enablePacking = true;
    config.password = "secret";

    ArchiveWriterImpl writer;
    ASSERT_TRUE(writer.init(archivePath.string(), config));
    ASSERT_TRUE(writer.addFile(makeDirectoryReader("assets")));
    ASSERT_TRUE(writer.addFile(makeFileReader("assets/repeated.txt", "AAAAAAAAAAAAAAAAAAAA")));
    ASSERT_TRUE(writer.addFile(makeFileReader("notes.txt", "chunked data")));
    ASSERT_TRUE(writer.finalize());

    ArchiveReaderImpl reader;
    ASSERT_TRUE(reader.open(archivePath.string(), config));

    FileMetaData meta{};
    ASSERT_TRUE(reader.getNextFileMeta(meta));
    EXPECT_EQ(meta.relativePath, "assets");
    EXPECT_TRUE(meta.isDirectory);

    ASSERT_TRUE(reader.getNextFileMeta(meta));
    EXPECT_EQ(meta.relativePath, "assets/repeated.txt");
    EXPECT_EQ(readCurrentFile(reader), "AAAAAAAAAAAAAAAAAAAA");

    ASSERT_TRUE(reader.getNextFileMeta(meta));
    EXPECT_EQ(meta.relativePath, "notes.txt");
    EXPECT_EQ(readCurrentFile(reader), "chunked data");

    EXPECT_FALSE(reader.getNextFileMeta(meta));
    reader.close();
}

TEST(ArchiveImplTest, EncryptedArchiveRequiresPasswordOnOpen) {
    TempDir temp("archive_password");
    const auto archivePath = temp.path() / "encrypted.dbak";

    BackupConfig config;
    config.enableEncryption = true;
    config.password = "secret";

    ArchiveWriterImpl writer;
    ASSERT_TRUE(writer.init(archivePath.string(), config));
    ASSERT_TRUE(writer.addFile(makeFileReader("secret.txt", "classified")));
    ASSERT_TRUE(writer.finalize());

    ArchiveReaderImpl reader;
    EXPECT_FALSE(reader.open(archivePath.string()));
}

TEST(ArchiveImplTest, ReaderRejectsInvalidMagic) {
    TempDir temp("archive_invalid");
    const auto archivePath = temp.path() / "invalid.dbak";
    std::ofstream out(archivePath, std::ios::binary);
    out << "not a databackup archive";
    out.close();

    ArchiveReaderImpl reader;
    EXPECT_FALSE(reader.open(archivePath.string()));
}

}  // namespace
}  // namespace backup
