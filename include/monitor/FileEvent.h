#pragma once

#include <string>
#include <cstdint>

namespace backup {

// 文件系统事件类型
enum class FileEventType {
    CREATED,    // 文件/目录被创建
    MODIFIED,   // 文件内容被修改
    DELETED,    // 文件/目录被删除
    MOVED_FROM, // 文件被移出监控目录
    MOVED_TO    // 文件被移入监控目录
};

// 文件系统事件结构体
struct FileEvent {
    std::string filePath;     // 发生变化的文件/目录的相对或绝对路径
    FileEventType type;       // 事件类型
    bool isDirectory;         // 变化的是否是目录
    uint64_t timestamp;       // 事件发生的时间戳
};

} // namespace backup