#pragma once

#include "monitor/IFileEventListener.h"
#include <string>
#include <memory>

namespace backup {

// 监控器配置
struct MonitorConfig {
    std::string watchPath;      // 需要监控的源目录
    bool runAsDaemon = false;   // 是否作为后台 Daemon 运行
    std::string pidFilePath;    // PID 文件路径（Daemon 模式下使用）
    std::string logFilePath;    // 日志文件路径（Daemon 模式下使用）
};

// 监控器核心接口（由 C同学 实现）
class IMonitor {
public:
    virtual ~IMonitor() = default;

    // 初始化并启动监控
    // listener: 实现了 IFileEventListener 接口的对象（由A提供）
    virtual bool start(const MonitorConfig& config, std::shared_ptr<IFileEventListener> listener) = 0;
    
    // 停止监控，释放 inotify 资源，清理 PID 文件
    virtual void stop() = 0;
    
    // 查询监控状态
    virtual bool isRunning() const = 0;
};

} // namespace backup