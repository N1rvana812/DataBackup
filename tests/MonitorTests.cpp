#include "monitor/Monitor.h"
#include "monitor/IFileEventListener.h"
#include "TestHarness.h"

#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

class TestListener : public backup::IFileEventListener {
public:
    void onFileEvent(const backup::FileEvent& event) override {
        std::lock_guard<std::mutex> lock(mutex_);
        events_.push_back(event);
        condition_.notify_all();
    }

    bool waitForEvent(std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(mutex_);
        return condition_.wait_for(lock, timeout, [this]() { return !events_.empty(); });
    }

    bool hasEventForPath(const std::string& filePath) const {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& event : events_) {
            if (event.filePath == filePath) {
                return true;
            }
        }
        return false;
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::vector<backup::FileEvent> events_;
};

static bool testStartWithoutListener() {
    backup::Monitor monitor;
    backup::MonitorConfig config;
    config.watchPath = std::filesystem::temp_directory_path().string();
    EXPECT_FALSE(monitor.start(config, nullptr));
    return true;
}

static bool testStartWithInvalidPath() {
    backup::Monitor monitor;
    backup::MonitorConfig config;
    config.watchPath = "/path/does/not/exist";
    auto listener = std::make_shared<TestListener>();
    EXPECT_FALSE(monitor.start(config, listener));
    return true;
}

static bool testStartTwiceFails() {
    backup::Monitor monitor;
    auto listener = std::make_shared<TestListener>();
    backup::MonitorConfig config;
    config.watchPath = std::filesystem::temp_directory_path().string();
    EXPECT_TRUE(monitor.start(config, listener));
    EXPECT_FALSE(monitor.start(config, listener));
    monitor.stop();
    return true;
}

static bool testMonitorReceivesCreateEvent() {
#ifdef __linux__
    const std::filesystem::path tempDir = std::filesystem::temp_directory_path() / std::filesystem::unique_path("databackup-monitor-%%%%-%%%%");
    EXPECT_TRUE(std::filesystem::create_directories(tempDir));

    backup::Monitor monitor;
    auto listener = std::make_shared<TestListener>();
    backup::MonitorConfig config;
    config.watchPath = tempDir.string();
    EXPECT_TRUE(monitor.start(config, listener));
    EXPECT_TRUE(monitor.isRunning());

    const std::filesystem::path testFile = tempDir / "file.txt";
    {
        std::ofstream output(testFile);
        EXPECT_TRUE(output.is_open());
        output << "hello";
    }

    EXPECT_TRUE(listener->waitForEvent(std::chrono::seconds(5)));
    EXPECT_TRUE(listener->hasEventForPath(testFile.string()));

    monitor.stop();
    EXPECT_FALSE(monitor.isRunning());
    std::filesystem::remove_all(tempDir);
    return true;
#else
    std::cout << "[ SKIPPED ] Monitor receive event tests are only supported on Linux." << std::endl;
    return true;
#endif
}

int main() {
    bool ok = true;
    ok &= runTest("MonitorStartWithoutListener", testStartWithoutListener);
    ok &= runTest("MonitorStartWithInvalidPath", testStartWithInvalidPath);
    ok &= runTest("MonitorStartTwiceFails", testStartTwiceFails);
    ok &= runTest("MonitorReceivesCreateEvent", testMonitorReceivesCreateEvent);
    return ok ? 0 : 1;
}
