#pragma once

#include "monitor/FileEvent.h"

namespace backup {
// 由A实现！！
// 文件事件监听器接口（由核心引擎 BackupEngine 实现）
class IFileEventListener {
public:
    virtual ~IFileEventListener() = default;

    // 【核心】当监控模块捕获到文件变化时，调用此方法通知引擎
    // 引擎收到通知后，自行决定是执行增量备份、还是更新过滤规则等
    virtual void onFileEvent(const FileEvent& event) = 0;
};

} // namespace backup