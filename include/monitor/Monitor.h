#pragma once

#include "monitor/IMonitor.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

namespace backup {

class Monitor : public IMonitor {
public:
    Monitor();
    ~Monitor() override;

    bool start(const MonitorConfig& config, std::shared_ptr<IFileEventListener> listener) override;
    void stop() override;
    bool isRunning() const override;

private:
    bool addWatch(const std::filesystem::path& path);
    bool initializeWatcher(const std::filesystem::path& rootPath);
    void watchLoop();
    void processEvent(uint32_t mask, const std::filesystem::path& path, bool isDirectory);
    bool writePidFile() const;
    void removePidFile() const;
    bool redirectLogFile(const std::string& filePath) const;

private:
    std::atomic<bool> running_;
    std::thread watchThread_;
    std::shared_ptr<IFileEventListener> listener_;
    MonitorConfig config_;
    int watchFd_;
    void* directoryHandle_;
    std::unordered_map<int, std::filesystem::path> descriptorToPath_;
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> lastEventTime_;
    mutable std::mutex mutex_;
    std::string lastError_;
};

} // namespace backup
