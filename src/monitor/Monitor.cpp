#include "monitor/Monitor.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <system_error>
#include <thread>
#include <vector>
#include <filesystem>
#include <utility>

#ifdef __linux__
#include <fcntl.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#elif defined(_WIN32)
#include <windows.h>
#endif

namespace backup {
namespace {
constexpr size_t kEventBufferSize = 4096;

bool createParentDirectory(const std::string& path) {
    const auto parent = std::filesystem::path(path).parent_path();
    if (parent.empty()) {
        return true;
    }

    std::error_code ec;
    std::filesystem::create_directories(parent, ec);
    return !ec;
}

#ifdef __linux__
bool daemonizeProcess(const std::string& logFilePath) {
    pid_t pid = fork();
    if (pid < 0) {
        return false;
    }

    if (pid > 0) {
        _exit(EXIT_SUCCESS);
    }

    if (::setsid() < 0) {
        return false;
    }

    umask(0);
    if (!logFilePath.empty()) {
        FILE* logFile = std::fopen(logFilePath.c_str(), "a");
        if (logFile != nullptr) {
            dup2(fileno(logFile), STDOUT_FILENO);
            dup2(fileno(logFile), STDERR_FILENO);
            std::fclose(logFile);
        }
    }

    return true;
}
#endif

} // namespace

Monitor::Monitor()
    : running_(false)
    , watchThread_()
    , listener_()
    , config_()
    , watchFd_(-1)
    , directoryHandle_(nullptr) {}

Monitor::~Monitor() {
    stop();
}

bool Monitor::start(const MonitorConfig& config, std::shared_ptr<IFileEventListener> listener) {
    lastError_.clear();

    if (!listener) {
        lastError_ = "listener must not be null";
        return false;
    }

    if (running_.load()) {
        lastError_ = "monitor is already running";
        return false;
    }

    config_ = config;
    listener_ = std::move(listener);

    if (config_.watchPath.empty()) {
        lastError_ = "watchPath must be specified";
        return false;
    }

    std::filesystem::path watchRoot = std::filesystem::absolute(config_.watchPath);
    config_.watchPath = watchRoot.string();
    std::error_code ec;
    if (!std::filesystem::exists(watchRoot, ec) || ec) {
        lastError_ = "watch path does not exist: " + watchRoot.string();
        return false;
    }

    if (!std::filesystem::is_directory(watchRoot, ec) || ec) {
        lastError_ = "watch path is not a directory: " + watchRoot.string();
        return false;
    }

    if (!config_.runAsDaemon && !config_.logFilePath.empty()) {
        if (!redirectLogFile(config_.logFilePath)) {
            lastError_ = "failed to redirect log file";
            return false;
        }
    }

#ifdef __linux__
    if (config_.runAsDaemon && !daemonizeProcess(config_.logFilePath)) {
        lastError_ = "failed to daemonize process";
        return false;
    }
#endif

    if (!config_.pidFilePath.empty() && !writePidFile()) {
        lastError_ = "failed to write pid file";
        return false;
    }

    if (!initializeWatcher(watchRoot)) {
        removePidFile();
        return false;
    }

    running_.store(true);
    watchThread_ = std::thread(&Monitor::watchLoop, this);
    return true;
}

void Monitor::stop() {
    if (!running_.exchange(false)) {
        return;
    }

#ifdef __linux__
    if (watchFd_ >= 0) {
        ::close(watchFd_);
        watchFd_ = -1;
    }
#elif defined(_WIN32)
    if (directoryHandle_) {
        CancelIoEx(static_cast<HANDLE>(directoryHandle_), nullptr);
        CloseHandle(static_cast<HANDLE>(directoryHandle_));
        directoryHandle_ = nullptr;
    }
#endif

    if (watchThread_.joinable()) {
        watchThread_.join();
    }

    removePidFile();
}

bool Monitor::isRunning() const {
    return running_.load();
}

const std::string& Monitor::lastError() const {
    return lastError_;
}
bool Monitor::addWatch(const std::filesystem::path& path) {
#ifdef __linux__
    int wd = ::inotify_add_watch(watchFd_, path.c_str(),
                                 IN_CREATE | IN_MODIFY | IN_DELETE | IN_MOVED_FROM |
                                 IN_MOVED_TO | IN_DELETE_SELF | IN_MOVE_SELF | IN_ATTRIB);
    if (wd < 0) {
        lastError_ = "inotify_add_watch failed for " + path.string() + ": " + std::strerror(errno);
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    descriptorToPath_.emplace(wd, path);
    return true;
#else
    return true;
#endif
}

bool Monitor::initializeWatcher(const std::filesystem::path& rootPath) {
#ifdef __linux__
    watchFd_ = ::inotify_init1(IN_CLOEXEC | IN_NONBLOCK);
    if (watchFd_ < 0) {
        lastError_ = std::string("inotify_init1 failed: ") + std::strerror(errno);
        return false;
    }

    if (!addWatch(rootPath)) {
        close(watchFd_);
        watchFd_ = -1;
        return false;
    }

    std::error_code ec;
    for (auto it = std::filesystem::recursive_directory_iterator(rootPath, ec);
         it != std::filesystem::recursive_directory_iterator();
         it.increment(ec)) {
        if (ec) {
            lastError_ = "failed to iterate watch tree: " + ec.message();
            continue;
        }

        if (!it->is_directory(ec)) {
            continue;
        }

        if (!addWatch(it->path())) {
            close(watchFd_);
            watchFd_ = -1;
            return false;
        }
    }

    return true;
#elif defined(_WIN32)
    directoryHandle_ = CreateFileW(rootPath.wstring().c_str(), FILE_LIST_DIRECTORY,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                   nullptr, OPEN_EXISTING,
                                   FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (directoryHandle_ == INVALID_HANDLE_VALUE) {
        lastError_ = "CreateFileW failed: " + std::to_string(GetLastError());
        directoryHandle_ = nullptr;
        return false;
    }
    return true;
#else
    return false;
#endif
}

void Monitor::watchLoop() {
#ifdef __linux__
    std::vector<char> buffer(kEventBufferSize);

    while (running_.load()) {
        ssize_t length = ::read(watchFd_, buffer.data(), static_cast<int>(buffer.size()));
        if (length < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                continue;
            }
            break;
        }

        if (length == 0) {
            continue;
        }

        size_t offset = 0;
        while (offset < static_cast<size_t>(length)) {
            const struct inotify_event* event = reinterpret_cast<const struct inotify_event*>(buffer.data() + offset);

            if (event->mask & IN_Q_OVERFLOW) {
                lastError_ = "inotify event queue overflow";
                offset += sizeof(struct inotify_event) + event->len;
                continue;
            }

            std::filesystem::path parentPath;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                auto it = descriptorToPath_.find(event->wd);
                if (it != descriptorToPath_.end()) {
                    parentPath = it->second;
                }
            }

            if (event->mask & IN_IGNORED) {
                std::lock_guard<std::mutex> lock(mutex_);
                descriptorToPath_.erase(event->wd);
            }

            if (parentPath.empty()) {
                offset += sizeof(struct inotify_event) + event->len;
                continue;
            }

            std::filesystem::path eventPath = parentPath;
            if (event->len > 0) {
                eventPath /= std::string(event->name);
            }

            bool isDirectory = (event->mask & IN_ISDIR) != 0;
            processEvent(event->mask, eventPath, isDirectory);

            offset += sizeof(struct inotify_event) + event->len;
        }
    }
    running_.store(false);
#elif defined(_WIN32)
    std::vector<BYTE> buffer(kEventBufferSize);

    while (running_.load()) {
        DWORD bytesReturned = 0;
        if (!ReadDirectoryChangesW(static_cast<HANDLE>(directoryHandle_),
                                   buffer.data(),
                                   static_cast<DWORD>(buffer.size()),
                                   TRUE,
                                   FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
                                   FILE_NOTIFY_CHANGE_ATTRIBUTES | FILE_NOTIFY_CHANGE_SIZE |
                                   FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_CREATION,
                                   &bytesReturned,
                                   nullptr,
                                   nullptr)) {
            DWORD error = GetLastError();
            if (error == ERROR_OPERATION_ABORTED || !running_.load()) {
                break;
            }
            lastError_ = "ReadDirectoryChangesW failed: " + std::to_string(error);
            break;
        }

        if (bytesReturned == 0) {
            continue;
        }

        size_t offset = 0;
        while (offset < bytesReturned) {
            const FILE_NOTIFY_INFORMATION* info = reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(buffer.data() + offset);
            std::wstring name(info->FileName, info->FileNameLength / sizeof(WCHAR));
            std::filesystem::path eventPath = std::filesystem::path(config_.watchPath) / name;
            std::error_code ec;
            bool isDirectory = std::filesystem::exists(eventPath, ec) && std::filesystem::is_directory(eventPath, ec);
            uint32_t mask = 0;
            switch (info->Action) {
                case FILE_ACTION_ADDED:
                    mask = 0x100000;
                    break;
                case FILE_ACTION_REMOVED:
                    mask = 0x200000;
                    break;
                case FILE_ACTION_MODIFIED:
                    mask = 0x400000;
                    break;
                case FILE_ACTION_RENAMED_OLD_NAME:
                    mask = 0x800000;
                    break;
                case FILE_ACTION_RENAMED_NEW_NAME:
                    mask = 0x1000000;
                    break;
                default:
                    mask = 0;
                    break;
            }

            if (mask != 0) {
                processEvent(mask, eventPath, isDirectory);
            }

            if (info->NextEntryOffset == 0) {
                break;
            }
            offset += info->NextEntryOffset;
        }
    }
    running_.store(false);
#endif
}

void Monitor::processEvent(uint32_t mask, const std::filesystem::path& path, bool isDirectory) {
    if (!listener_) {
        return;
    }

#ifdef __linux__
    const bool createdMask = mask & (IN_CREATE | IN_MOVED_TO);
    const bool deletedMask = mask & (IN_DELETE | IN_DELETE_SELF);
    const bool movedFromMask = mask & (IN_MOVED_FROM | IN_MOVE_SELF);
    const bool movedToMask = mask & IN_MOVED_TO;
    const bool modifiedMask = mask & (IN_MODIFY | IN_ATTRIB);
#else
    const bool createdMask = mask & 0x100000;
    const bool deletedMask = mask & 0x200000;
    const bool movedFromMask = mask & 0x800000;
    const bool movedToMask = mask & 0x1000000;
    const bool modifiedMask = mask & 0x400000;
#endif

    FileEventType type;
    if (createdMask) {
        type = FileEventType::CREATED;
    } else if (deletedMask) {
        type = FileEventType::DELETED;
    } else if (movedFromMask) {
        type = FileEventType::MOVED_FROM;
    } else if (movedToMask) {
        type = FileEventType::MOVED_TO;
    } else if (modifiedMask) {
        type = FileEventType::MODIFIED;
    } else {
        return;
    }

    const std::filesystem::path absolutePath = std::filesystem::absolute(path);
    FileEvent event;
    event.filePath = absolutePath.string();
    event.type = type;
    event.isDirectory = isDirectory;
    event.timestamp = static_cast<uint64_t>(std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()));
    const std::string normalizedPath = absolutePath.string();
    const std::string debounceKey = normalizedPath + "|" + std::to_string(static_cast<int>(type));
    const auto now = std::chrono::steady_clock::now();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = lastEventTime_.find(debounceKey);
        if (it != lastEventTime_.end() && now - it->second < std::chrono::milliseconds(100)) {
            return;
        }
        lastEventTime_[debounceKey] = now;
    }

    listener_->onFileEvent(event);

#ifdef __linux__
    if (isDirectory && (mask & (IN_CREATE | IN_MOVED_TO))) {
        addWatch(path);
    }
#endif
}

bool Monitor::writePidFile() const {
#ifdef __linux__
    if (config_.pidFilePath.empty()) {
        return true;
    }

    if (!createParentDirectory(config_.pidFilePath)) {
        return false;
    }

    std::ofstream pidFile(config_.pidFilePath, std::ios::out | std::ios::trunc);
    if (!pidFile.is_open()) {
        return false;
    }

    pidFile << ::getpid() << '\n';
    return pidFile.good();
#else
    return false;
#endif
}

void Monitor::removePidFile() const {
#ifdef __linux__
    if (config_.pidFilePath.empty()) {
        return;
    }

    std::error_code ec;
    std::filesystem::remove(config_.pidFilePath, ec);
#endif
}

bool Monitor::redirectLogFile(const std::string& filePath) const {
#ifdef __linux__
    if (filePath.empty()) {
        return true;
    }

    if (!createParentDirectory(filePath)) {
        return false;
    }

    FILE* logFile = std::fopen(filePath.c_str(), "a");
    if (!logFile) {
        return false;
    }

    if (dup2(fileno(logFile), STDOUT_FILENO) < 0) {
        return false;
    }

    if (dup2(fileno(logFile), STDERR_FILENO) < 0) {
        return false;
    }

    return true;
#else
    return false;
#endif
}

} // namespace backup
