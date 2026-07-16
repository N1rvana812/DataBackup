#pragma once

#include "common/PlatformCompat.h"

#include <string>
#include <cstdint>

namespace backup {

// 文件元数据结构（A提取，B写入归档头，还原时A恢复）
struct FileMetaData {
    std::string relativePath; // 相对路径（归档时只存相对路径，还原时拼接目标路径）
    mode_t permissions;       // 文件权限 
    uid_t ownerId;            // 属主ID
    gid_t groupId;            // 属组ID
    time_t accessTime;        // 访问时间
    time_t modifyTime;        // 修改时间
    bool isDirectory;         // 是否是目录
    uint64_t fileSize;        // 原始文件大小
};

// 备份配置结构（控制B的管道行为）
struct BackupConfig {
    bool enableCompression = false;
    bool enableEncryption = false;
    bool enablePacking = false;
    std::string password;     // 加密密码
    int compressionLevel = 6; // 压缩级别 (1-9)
};

} 