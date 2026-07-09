#pragma once

#include "common/Types.h"
#include "core/IFileReader.h"
#include <string>
#include <memory>

namespace backup {

class IArchiveWriter {
public:
    virtual ~IArchiveWriter() = default;

    // 初始化归档文件，写入全局Header（包含配置信息、魔数等）
    virtual bool init(const std::string& archivePath, const BackupConfig& config) = 0;
    
    // 【核心】将文件加入归档。
    // B内部会：1. 写入文件Header(元数据)；2. 循环调用 reader->readChunk() 读数据；
    //         3. 对数据进行压缩/加密；4. 写入归档文件。
    virtual bool addFile(std::shared_ptr<IFileReader> reader) = 0;
    
    // 结束归档，写入尾部索引（可选），关闭文件
    virtual bool finalize() = 0;
};

} 