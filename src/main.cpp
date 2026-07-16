#include "common/Types.h"
#include "core/BackupEngine.h"
#include "core/FileFilter.h"
#include "monitor/Monitor.h"
#include "pipeline/ArchiveReaderImpl.h"
#include "pipeline/ArchiveWriterImpl.h"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <iostream>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

void printUsage(const char* progName) {
    std::cout << "DataBackup - High-efficiency secure data backup tool\n\n"
              << "Usage:\n"
              << "  " << progName << " backup -s <source> -d <dest> [options]\n"
              << "  " << progName << " restore -s <archive> -d <target> [options]\n"
              << "  " << progName << " watch -s <source> -d <dest> [options]\n\n"
              << "Backup Options:\n"
              << "  -s, --source <path>     Source directory to backup\n"
              << "  -d, --dest <path>       Destination archive file (.dbak)\n"
              << "  --compress              Enable RLE compression\n"
              << "  --pack                  Enable multi-file packing\n"
              << "  --encrypt               Enable RC4 stream encryption\n"
              << "  --password <pwd>        Encryption password\n"
              << "  --level <1-9>           Compression level (default: 6)\n"
              << "  --exclude <pattern>     Exclude files by glob pattern, repeatable\n"
              << "  --include-ext <ext>     Include only extension, e.g. .txt, repeatable\n"
              << "  --min-size <bytes>      Include files at least this size\n"
              << "  --max-size <bytes>      Include files at most this size\n"
              << "  --modified-after <ts>   Include files modified at/after Unix timestamp\n"
              << "  --modified-before <ts>  Include files modified at/before Unix timestamp\n"
              << "  --owner <uid>           Include files owned by numeric UID\n"
              << "  --exclude-hidden        Exclude hidden files/directories\n"
              << "  --exclude-dirs          Exclude directory entries from archive\n\n"
              << "Restore Options:\n"
              << "  -s, --source <path>     Source archive file (.dbak)\n"
              << "  -d, --dest <path>       Target directory to restore to\n"
              << "  --password <pwd>        Decryption password\n\n"
              << "Examples:\n"
              << "  # Basic backup\n"
              << "  " << progName << " backup -s /home/user/data -d /mnt/backup/data.dbak\n\n"
              << "  # Compressed backup\n"
              << "  " << progName << " backup -s /home/user/data -d /mnt/backup/data.dbak --compress\n\n"
              << "  # Encrypted backup\n"
              << "  " << progName << " backup -s /home/user/data -d /mnt/backup/data.dbak \\\n"
              << "      --compress --encrypt --password MySecretKey\n\n"
              << "  # Restore\n"
              << "  " << progName << " restore -s /mnt/backup/data.dbak -d /home/user/restored\n\n"
              << "  # Restore encrypted archive\n"
              << "  " << progName << " restore -s /mnt/backup/data.dbak -d /home/user/restored \\\n"
              << "      --password MySecretKey\n";
}

struct CliArgs {
    std::string command;
    std::string source;
    std::string dest;
    bool compress = false;
    bool encrypt = false;
    bool pack = false;
    bool daemon = false;
    std::string password;
    int compressionLevel = 6;
    backup::FilterOptions filterOptions;
};

bool parseUnsigned(const std::string& text, uint64_t& value) {
    try {
        size_t consumed = 0;
        const unsigned long long parsed = std::stoull(text, &consumed, 10);
        if (consumed != text.size()) {
            return false;
        }
        value = static_cast<uint64_t>(parsed);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool parseTimeValue(const std::string& text, time_t& value) {
    try {
        size_t consumed = 0;
        const long long parsed = std::stoll(text, &consumed, 10);
        if (consumed != text.size()) {
            return false;
        }
        value = static_cast<time_t>(parsed);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

std::string normalizeExtension(std::string extension) {
    if (!extension.empty() && extension.front() != '.') {
        extension.insert(extension.begin(), '.');
    }
    return extension;
}

bool parseArgs(int argc, char* argv[], CliArgs& args) {
    if (argc < 2) {
        return false;
    }

    args.command = argv[1];

    if (args.command != "backup" && args.command != "restore" && args.command != "watch") {
        return false;
    }

    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "-s" || arg == "--source") {
            if (++i >= argc) {
                std::cerr << "[ERROR] Missing value for " << arg << '\n';
                return false;
            }
            args.source = argv[i];
        } else if (arg == "-d" || arg == "--dest") {
            if (++i >= argc) {
                std::cerr << "[ERROR] Missing value for " << arg << '\n';
                return false;
            }
            args.dest = argv[i];
        } else if (arg == "--compress") {
            args.compress = true;
        } else if (arg == "--encrypt") {
            args.encrypt = true;
        } else if (arg == "--pack") {
            args.pack = true;
        } else if (arg == "--daemon") {
            args.daemon = true;
        } else if (arg == "--password") {
            if (++i >= argc) {
                std::cerr << "[ERROR] Missing value for " << arg << '\n';
                return false;
            }
            args.password = argv[i];
        } else if (arg == "--level") {
            if (++i >= argc) {
                std::cerr << "[ERROR] Missing value for " << arg << '\n';
                return false;
            }
            args.compressionLevel = std::atoi(argv[i]);
            if (args.compressionLevel < 1 || args.compressionLevel > 9) {
                std::cerr << "[ERROR] Compression level must be 1-9\n";
                return false;
            }
        } else if (arg == "--exclude") {
            if (++i >= argc) {
                std::cerr << "[ERROR] Missing value for " << arg << '\n';
                return false;
            }
            args.filterOptions.excludePatterns.push_back(argv[i]);
        } else if (arg == "--include-ext") {
            if (++i >= argc) {
                std::cerr << "[ERROR] Missing value for " << arg << '\n';
                return false;
            }
            args.filterOptions.includeExtensions.push_back(normalizeExtension(argv[i]));
        } else if (arg == "--min-size") {
            if (++i >= argc) {
                std::cerr << "[ERROR] Missing value for " << arg << '\n';
                return false;
            }
            uint64_t value = 0;
            if (!parseUnsigned(argv[i], value)) {
                std::cerr << "[ERROR] --min-size must be a non-negative integer\n";
                return false;
            }
            args.filterOptions.minFileSize = value;
        } else if (arg == "--max-size") {
            if (++i >= argc) {
                std::cerr << "[ERROR] Missing value for " << arg << '\n';
                return false;
            }
            uint64_t value = 0;
            if (!parseUnsigned(argv[i], value)) {
                std::cerr << "[ERROR] --max-size must be a non-negative integer\n";
                return false;
            }
            args.filterOptions.maxFileSize = value;
        } else if (arg == "--modified-after") {
            if (++i >= argc) {
                std::cerr << "[ERROR] Missing value for " << arg << '\n';
                return false;
            }
            time_t value = 0;
            if (!parseTimeValue(argv[i], value)) {
                std::cerr << "[ERROR] --modified-after must be a Unix timestamp\n";
                return false;
            }
            args.filterOptions.hasMinModifyTime = true;
            args.filterOptions.minModifyTime = value;
        } else if (arg == "--modified-before") {
            if (++i >= argc) {
                std::cerr << "[ERROR] Missing value for " << arg << '\n';
                return false;
            }
            time_t value = 0;
            if (!parseTimeValue(argv[i], value)) {
                std::cerr << "[ERROR] --modified-before must be a Unix timestamp\n";
                return false;
            }
            args.filterOptions.hasMaxModifyTime = true;
            args.filterOptions.maxModifyTime = value;
        } else if (arg == "--owner") {
            if (++i >= argc) {
                std::cerr << "[ERROR] Missing value for " << arg << '\n';
                return false;
            }
            uint64_t value = 0;
            if (!parseUnsigned(argv[i], value) || value > std::numeric_limits<uid_t>::max()) {
                std::cerr << "[ERROR] --owner must be a numeric UID\n";
                return false;
            }
            args.filterOptions.hasOwnerId = true;
            args.filterOptions.ownerId = static_cast<uid_t>(value);
        } else if (arg == "--exclude-hidden") {
            args.filterOptions.includeHidden = false;
        } else if (arg == "--exclude-dirs") {
            args.filterOptions.includeDirectories = false;
        } else if (arg == "--help" || arg == "-h") {
            return false;
        } else {
            std::cerr << "[ERROR] Unknown option: " << arg << '\n';
            return false;
        }
    }

    if (args.source.empty()) {
        std::cerr << "[ERROR] Source path is required (-s/--source)\n";
        return false;
    }
    if (args.dest.empty()) {
        std::cerr << "[ERROR] Destination path is required (-d/--dest)\n";
        return false;
    }

    if ((args.command == "backup" || args.command == "watch") && args.encrypt && args.password.empty()) {
        std::cerr << "[ERROR] Password is required when encryption is enabled (--password)\n";
        return false;
    }
    if (args.filterOptions.minFileSize > args.filterOptions.maxFileSize) {
        std::cerr << "[ERROR] --min-size cannot be greater than --max-size\n";
        return false;
    }
    if (args.filterOptions.hasMinModifyTime && args.filterOptions.hasMaxModifyTime &&
        args.filterOptions.minModifyTime > args.filterOptions.maxModifyTime) {
        std::cerr << "[ERROR] --modified-after cannot be later than --modified-before\n";
        return false;
    }

    return true;
}

backup::BackupConfig buildConfig(const CliArgs& args) {
    backup::BackupConfig config;
    config.enableCompression = args.compress;
    config.enableEncryption = args.encrypt;
    config.enablePacking = args.pack;
    config.password = args.password;
    config.compressionLevel = args.compressionLevel;
    return config;
}

std::filesystem::path resolveArchivePath(const CliArgs& args) {
    const std::filesystem::path dest(args.dest);
    if (dest.empty()) {
        return {};
    }

    std::error_code ec;
    if (std::filesystem::exists(dest, ec) && !ec && std::filesystem::is_directory(dest, ec) && !ec) {
        return dest / "databackup-watch.dbak";
    }

    return dest;
}

int runBackup(const CliArgs& args) {
    const auto config = buildConfig(args);
    const std::filesystem::path sourceRoot(args.source);
    const std::filesystem::path archivePath(args.dest);

    if (!std::filesystem::exists(sourceRoot)) {
        std::cerr << "[ERROR] Source path does not exist: " << sourceRoot << '\n';
        return 1;
    }
    if (!std::filesystem::is_directory(sourceRoot)) {
        std::cerr << "[ERROR] Source path is not a directory: " << sourceRoot << '\n';
        return 1;
    }

    std::cout << "[INFO] Starting backup:\n"
              << "  Source:      " << sourceRoot << '\n'
              << "  Destination: " << archivePath << '\n'
              << "  Compression: " << (config.enableCompression ? "yes" : "no") << '\n'
              << "  Packing:     " << (config.enablePacking ? "yes" : "no") << '\n'
              << "  Encryption:  " << (config.enableEncryption ? "yes (RC4 stream cipher)" : "no") << '\n';

    if (config.enableCompression) {
        std::cout << "  Comp. level: " << config.compressionLevel << '\n';
    }

    backup::BackupEngine engine;
    backup::ArchiveWriterImpl writer;

    if (!engine.backupDirectory(sourceRoot, archivePath, writer, config, args.filterOptions)) {
        std::cerr << "[ERROR] Backup failed: " << engine.lastError() << '\n';
        return 1;
    }

    const auto& stats = engine.stats();
    std::cout << "[INFO] Backup completed successfully!\n"
              << "  Files:       " << stats.filesProcessed << '\n'
              << "  Directories: " << stats.directoriesProcessed << '\n'
              << "  Bytes:       " << stats.bytesProcessed << '\n';

    return 0;
}

int runWatch(const CliArgs& args) {
    const auto config = buildConfig(args);
    const std::filesystem::path sourceRoot(args.source);
    const std::filesystem::path archivePath = resolveArchivePath(args);

    if (!std::filesystem::exists(sourceRoot)) {
        std::cerr << "[ERROR] Source path does not exist: " << sourceRoot << '\n';
        return 1;
    }
    if (!std::filesystem::is_directory(sourceRoot)) {
        std::cerr << "[ERROR] Source path is not a directory: " << sourceRoot << '\n';
        return 1;
    }
    if (archivePath.empty()) {
        std::cerr << "[ERROR] Destination path is required for watch mode (-d/--dest)\n";
        return 1;
    }

    std::cout << "[INFO] Starting real-time backup watch:\n"
              << "  Source:      " << sourceRoot << '\n'
              << "  Destination: " << archivePath << '\n'
              << "  Compression: " << (config.enableCompression ? "yes" : "no") << '\n'
              << "  Packing:     " << (config.enablePacking ? "yes" : "no") << '\n'
              << "  Encryption:  " << (config.enableEncryption ? "yes (RC4 stream cipher)" : "no") << '\n';

    auto engine = std::make_shared<backup::BackupEngine>();
    std::mutex backupMutex;
    std::chrono::steady_clock::time_point lastBackupTime;
    const auto backupOnce = [&](const backup::FileEvent&) {
        const auto now = std::chrono::steady_clock::now();
        std::lock_guard<std::mutex> lock(backupMutex);
        if (lastBackupTime.time_since_epoch().count() != 0 &&
            now - lastBackupTime < std::chrono::seconds(1)) {
            return;
        }
        lastBackupTime = now;
        std::cout << "[INFO] Detected file change, triggering backup..." << std::endl;
        backup::ArchiveWriterImpl writer;
        if (!engine->backupDirectory(sourceRoot, archivePath, writer, config, args.filterOptions)) {
            std::cerr << "[ERROR] Incremental backup failed: " << engine->lastError() << '\n';
            return;
        }
        const auto& stats = engine->stats();
        std::cout << "[INFO] Backup completed: files=" << stats.filesProcessed
                  << ", directories=" << stats.directoriesProcessed
                  << ", bytes=" << stats.bytesProcessed << std::endl;
    };

    engine->setIncrementalHandler(backupOnce);

    backup::Monitor monitor;
    backup::MonitorConfig monitorConfig;
    monitorConfig.watchPath = sourceRoot.string();
    monitorConfig.runAsDaemon = args.daemon;
    if (args.daemon) {
        std::filesystem::path monitorDir = archivePath.parent_path();
        if (monitorDir.empty()) {
            monitorDir = std::filesystem::current_path();
        }
        monitorConfig.pidFilePath = (monitorDir / "databackup-watch.pid").string();
        monitorConfig.logFilePath = (monitorDir / "databackup-watch.log").string();
    }

    if (!monitor.start(monitorConfig, engine)) {
        std::cerr << "[ERROR] Failed to start monitor: " << monitor.lastError() << '\n';
        return 1;
    }

    std::cout << "[INFO] Watching for changes. Press Ctrl+C to stop." << std::endl;
    while (monitor.isRunning()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    return 0;
}

int runRestore(const CliArgs& args) {
    const auto config = buildConfig(args);
    const std::filesystem::path archivePath(args.source);
    const std::filesystem::path targetRoot(args.dest);

    if (!std::filesystem::exists(archivePath)) {
        std::cerr << "[ERROR] Archive file does not exist: " << archivePath << '\n';
        return 1;
    }

    std::cout << "[INFO] Starting restore:\n"
              << "  Archive:     " << archivePath << '\n'
              << "  Destination: " << targetRoot << '\n';

    backup::BackupEngine engine;
    backup::ArchiveReaderImpl reader;

    if (!engine.restoreArchive(archivePath, reader, targetRoot, config)) {
        std::cerr << "[ERROR] Restore failed: " << engine.lastError() << '\n';
        return 1;
    }

    const auto& stats = engine.stats();
    std::cout << "[INFO] Restore completed successfully!\n"
              << "  Files:       " << stats.filesProcessed << '\n'
              << "  Directories: " << stats.directoriesProcessed << '\n'
              << "  Bytes:       " << stats.bytesProcessed << '\n';

    return 0;
}

} // namespace

int main(int argc, char* argv[]) {
    CliArgs args;

    if (!parseArgs(argc, argv, args)) {
        printUsage(argv[0]);
        return (argc >= 2 && (std::strcmp(argv[1], "--help") == 0 ||
                               std::strcmp(argv[1], "-h") == 0)) ? 0 : 1;
    }

    if (args.command == "backup") {
        return runBackup(args);
    }

    if (args.command == "watch") {
        return runWatch(args);
    }

    if (args.command == "restore") {
        return runRestore(args);
    }

    printUsage(argv[0]);
    return 1;
}
