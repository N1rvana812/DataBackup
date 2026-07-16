#pragma once

#include "common/Types.h"
#include <cstdint>
#include <cstddef>
#include <string>

namespace backup {

class IFileReader {
public:
    virtual ~IFileReader() = default;

    // 打开指定相对路径的文件（A内部会拼接上真实的源目录前缀）
    virtual bool open(const std::string& relativePath) = 0;
    
    // 流式读取数据块。
    // buffer: B提供的缓冲区；size: 缓冲区大小。
    // 返回值: 实际读取的字节数。返回0表示读到文件末尾(EOF)，返回-1表示错误。
    virtual ssize_t readChunk(uint8_t* buffer, size_t size) = 0;
    
    // 获取该文件的元数据（A通过 stat() 系统调用获取）
    virtual FileMetaData getMetaData() const = 0;
    
    virtual void close() = 0;
};

} 