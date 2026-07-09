#pragma once

#include "common/Types.h"
#include <string>
#include <cstdint>
#include <cstddef>
#include <sys/types.h> // 用于 ssize_t

namespace backup {

class IArchiveReader {
public:
    virtual ~IArchiveReader() = default;

    virtual bool open(const std::string& archivePath) = 0;
    
    // 读取下一个文件的元数据。如果到达归档末尾，返回false。
    virtual bool getNextFileMeta(FileMetaData& meta) = 0;
    
    // 【核心】流式读取当前文件的内容（B内部已处理解密/解压）。
    // 返回值含义同 IFileReader::readChunk。
    virtual ssize_t readChunk(uint8_t* buffer, size_t size) = 0;
    
    virtual void close() = 0;
};

} // namespace backup