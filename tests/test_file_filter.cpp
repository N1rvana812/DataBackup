/**
 * @file test_file_filter.cpp
 * @brief Google Test unit tests for FileFilter
 *
 * Tests include/exclude logic, pattern matching, size filtering,
 * hidden file filtering, and directory filtering.
 */

#include "core/FileFilter.h"

#include <gtest/gtest.h>

namespace backup {
namespace {

// ============================================================================
// Helper to create FileMetaData with minimal fields
// ============================================================================

FileMetaData makeMeta(const std::string& path,
                      bool isDir = false,
                      uint64_t size = 1024) {
    FileMetaData meta{};
    meta.relativePath = path;
    meta.isDirectory = isDir;
    meta.fileSize = size;
    return meta;
}

// ============================================================================
// Default filter (no restrictions)
// ============================================================================

TEST(FileFilterTest, DefaultFilterIncludesRegularFile) {
    FileFilter filter;
    EXPECT_TRUE(filter.shouldInclude(makeMeta("file.txt")));
}

TEST(FileFilterTest, DefaultFilterIncludesDirectory) {
    FileFilter filter;
    EXPECT_TRUE(filter.shouldInclude(makeMeta("subdir", true)));
}

TEST(FileFilterTest, DefaultFilterIncludesHiddenFile) {
    FileFilter filter;
    EXPECT_TRUE(filter.shouldInclude(makeMeta(".hidden")));
}

// ============================================================================
// Directory filtering
// ============================================================================

TEST(FileFilterTest, ExcludeDirectories) {
    FilterOptions opts;
    opts.includeDirectories = false;
    FileFilter filter(opts);

    EXPECT_FALSE(filter.shouldInclude(makeMeta("subdir", true)));
    EXPECT_TRUE(filter.shouldInclude(makeMeta("file.txt", false)));
}

// ============================================================================
// Hidden file filtering
// ============================================================================

TEST(FileFilterTest, ExcludeHiddenFiles) {
    FilterOptions opts;
    opts.includeHidden = false;
    FileFilter filter(opts);

    EXPECT_FALSE(filter.shouldInclude(makeMeta(".hidden_file")));
    EXPECT_FALSE(filter.shouldInclude(makeMeta(".hidden_dir/file.txt")));
    EXPECT_TRUE(filter.shouldInclude(makeMeta("normal_file.txt")));
}

TEST(FileFilterTest, ExcludeHiddenDirectories) {
    FilterOptions opts;
    opts.includeHidden = false;
    FileFilter filter(opts);

    EXPECT_FALSE(filter.shouldInclude(makeMeta(".git", true)));
    EXPECT_TRUE(filter.shouldInclude(makeMeta("src", true)));
}

// ============================================================================
// Extension filtering
// ============================================================================

TEST(FileFilterTest, IncludeExtensionsMatch) {
    FilterOptions opts;
    opts.includeExtensions = {".txt", ".md"};
    FileFilter filter(opts);

    EXPECT_TRUE(filter.shouldInclude(makeMeta("readme.md")));
    EXPECT_TRUE(filter.shouldInclude(makeMeta("notes.txt")));
    EXPECT_FALSE(filter.shouldInclude(makeMeta("image.png")));
    EXPECT_FALSE(filter.shouldInclude(makeMeta("script.sh")));
}

TEST(FileFilterTest, IncludeExtensionsDoesNotAffectDirectories) {
    FilterOptions opts;
    opts.includeExtensions = {".txt"};
    FileFilter filter(opts);

    // Directories should still pass even though they have no extension
    EXPECT_TRUE(filter.shouldInclude(makeMeta("docs", true)));
}

TEST(FileFilterTest, IncludeExtensionsEmptyAllowsAll) {
    FilterOptions opts;
    opts.includeExtensions = {};  // empty = no filtering
    FileFilter filter(opts);

    EXPECT_TRUE(filter.shouldInclude(makeMeta("file.any")));
    EXPECT_TRUE(filter.shouldInclude(makeMeta("readme")));
}

// ============================================================================
// File size filtering
// ============================================================================

TEST(FileFilterTest, MinFileSizeFilter) {
    FilterOptions opts;
    opts.minFileSize = 100;
    FileFilter filter(opts);

    EXPECT_FALSE(filter.shouldInclude(makeMeta("small.txt", false, 50)));
    EXPECT_TRUE(filter.shouldInclude(makeMeta("large.txt", false, 200)));
    // Directories are not subject to size filtering
    EXPECT_TRUE(filter.shouldInclude(makeMeta("dir", true, 0)));
}

TEST(FileFilterTest, MaxFileSizeFilter) {
    FilterOptions opts;
    opts.maxFileSize = 1000;
    FileFilter filter(opts);

    EXPECT_TRUE(filter.shouldInclude(makeMeta("small.txt", false, 500)));
    EXPECT_FALSE(filter.shouldInclude(makeMeta("large.txt", false, 2000)));
}

TEST(FileFilterTest, MinAndMaxFileSizeRange) {
    FilterOptions opts;
    opts.minFileSize = 100;
    opts.maxFileSize = 1000;
    FileFilter filter(opts);

    EXPECT_FALSE(filter.shouldInclude(makeMeta("tiny.txt", false, 50)));
    EXPECT_TRUE(filter.shouldInclude(makeMeta("mid.txt", false, 500)));
    EXPECT_FALSE(filter.shouldInclude(makeMeta("huge.txt", false, 2000)));
}

// ============================================================================
// Exclude patterns (fnmatch/glob)
// ============================================================================

TEST(FileFilterTest, ExcludePatternsExactName) {
    FilterOptions opts;
    opts.excludePatterns = {"*.log"};
    FileFilter filter(opts);

    EXPECT_FALSE(filter.shouldInclude(makeMeta("debug.log")));
    EXPECT_TRUE(filter.shouldInclude(makeMeta("debug.txt")));
}

TEST(FileFilterTest, ExcludePatternsWildcardPath) {
    FilterOptions opts;
    opts.excludePatterns = {"build/*"};
    FileFilter filter(opts);

    EXPECT_FALSE(filter.shouldInclude(makeMeta("build/output.o")));
    EXPECT_TRUE(filter.shouldInclude(makeMeta("src/main.cpp")));
}

TEST(FileFilterTest, ExcludePatternsMultiplePatterns) {
    FilterOptions opts;
    opts.excludePatterns = {"*.o", "*.tmp", ".git/*"};
    FileFilter filter(opts);

    EXPECT_FALSE(filter.shouldInclude(makeMeta("program.o")));
    EXPECT_FALSE(filter.shouldInclude(makeMeta("data.tmp")));
    EXPECT_FALSE(filter.shouldInclude(makeMeta(".git/config")));
    EXPECT_TRUE(filter.shouldInclude(makeMeta("main.cpp")));
}

// ============================================================================
// Combined filters
// ============================================================================

TEST(FileFilterTest, CombinedFilters) {
    FilterOptions opts;
    opts.includeExtensions = {".cpp", ".h"};
    opts.minFileSize = 10;
    opts.maxFileSize = 10000;
    opts.excludePatterns = {"test/*"};
    opts.includeHidden = false;
    FileFilter filter(opts);

    // Matches extension and size, not excluded
    EXPECT_TRUE(filter.shouldInclude(makeMeta("src/main.cpp", false, 500)));

    // Wrong extension
    EXPECT_FALSE(filter.shouldInclude(makeMeta("src/main.rs", false, 500)));

    // Too small
    EXPECT_FALSE(filter.shouldInclude(makeMeta("src/main.cpp", false, 5)));

    // Excluded by pattern
    EXPECT_FALSE(filter.shouldInclude(makeMeta("test/main.cpp", false, 500)));

    // Hidden
    EXPECT_FALSE(filter.shouldInclude(makeMeta(".secret/main.cpp", false, 500)));
}

}  // namespace
}  // namespace backup
