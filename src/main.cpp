#include "common/Types.h"
#include "core/BackupEngine.h"
#include "core/FileFilter.h"
#include "pipeline/ArchiveReaderImpl.h"
#include "pipeline/ArchiveWriterImpl.h"

#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {

void printUsage(const char* progName) {
    std::cout << "DataBackup - High-efficiency secure data backup tool\n\n"
              << "Usage:\n"
              << "  " << progName << " backup -s <source> -d <dest> [options]\n"
              << "  " << progName << " restore -s <archive> -d <target> [options]\n\n"
              << "Backup Options:\n"
              << "  -s, --source <path>     Source directory to backup\n"
              << "  -d, --dest <path>       Destination archive file (.dbak)\n"
              << "  --compress              Enable RLE compression\n"
              << "  --encrypt               Enable RC4 stream encryption\n"
              << "  --password <pwd>        Encryption password\n"
              << "  --level <1-9>           Compression level (default: 6)\n\n"
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
    std::string password;
    int compressionLevel = 6;
};

bool parseArgs(int argc, char* argv[], CliArgs& args) {
    if (argc < 2) {
        return false;
    }

    args.command = argv[1];

    if (args.command != "backup" && args.command != "restore") {
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

    if (args.command == "backup" && args.encrypt && args.password.empty()) {
        std::cerr << "[ERROR] Password is required when encryption is enabled (--password)\n";
        return false;
    }

    return true;
}

backup::BackupConfig buildConfig(const CliArgs& args) {
    backup::BackupConfig config;
    config.enableCompression = args.compress;
    config.enableEncryption = args.encrypt;
    config.password = args.password;
    config.compressionLevel = args.compressionLevel;
    return config;
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
              << "  Encryption:  " << (config.enableEncryption ? "yes (RC4 stream cipher)" : "no") << '\n';

    if (config.enableCompression) {
        std::cout << "  Comp. level: " << config.compressionLevel << '\n';
    }

    backup::BackupEngine engine;
    backup::ArchiveWriterImpl writer;

    if (!engine.backupDirectory(sourceRoot, archivePath, writer, config)) {
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

    if (args.command == "restore") {
        return runRestore(args);
    }

    printUsage(argv[0]);
    return 1;
}
