#pragma once

#include "common/Types.h"
#include <cstdint>
#include <cstddef>
#include <string>

namespace backup {

class IFileWriter {
public:
    virtual ~IFileWriter() = default;

    // 打开目标文件（A内部会拼接上真实的目标目录前缀，并自动创建父目录）
    virtual bool open(const std::string& relativePath) = 0;
    
    // 写入数据块
    virtual bool writeChunk(const uint8_t* buffer, size_t size) = 0;
    
    // 恢复元数据（A内部调用 chmod, chown, utimes 等系统API）
    virtual bool applyMetaData(const FileMetaData& meta) = 0;
    
    virtual void close() = 0;
};

} // namespace backup