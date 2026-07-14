# 核心引擎模块

> `include/core/` · `src/core/` | 文件遍历、过滤、读写、备份调度

## 组件

| 类/文件 | 说明 |
|---|---|
| `DirectoryTraverser` | 递归遍历源目录，提取文件元数据 |
| `FileFilter` | 自定义规则过滤器（glob 模式、扩展名、大小、隐藏文件） |
| `IFileReader` / `FileSystemReader` | 流式文件读取接口与实现 |
| `IFileWriter` / `FileSystemWriter` | 文件写入与元数据恢复接口与实现 |
| `BackupEngine` | 备份调度引擎，编排遍历→读取→写入管道 |

## DirectoryTraverser

递归遍历源目录，跳过符号链接，返回按相对路径排序的 `FileMetaData` 列表。

```cpp
DirectoryTraverser traverser(sourceRoot, FileFilter(filterOptions));
auto entries = traverser.scan();  // → vector<FileMetaData>
```

- 使用 `std::filesystem::recursive_directory_iterator`
- 自动跳过权限不足的目录
- 通过 `lstat` 提取 POSIX 元数据（权限、属主、时间戳）
- 错误信息通过 `errors()` 获取

## FileFilter

支持以下过滤规则（`FilterOptions`）：

| 规则 | 字段 | 说明 |
|---|---|---|
| 排除模式 | `excludePatterns` | glob 匹配（如 `*.log`, `build/*`），使用 `fnmatch` |
| 扩展名白名单 | `includeExtensions` | 仅包含指定扩展名（如 `.txt`, `.md`） |
| 文件大小范围 | `minFileSize` / `maxFileSize` | 过滤过大/过小的文件 |
| 隐藏文件 | `includeHidden` | 是否包含 `.` 开头的文件/目录 |
| 目录 | `includeDirectories` | 是否包含目录条目 |

## IFileReader / FileSystemReader

流式文件读取接口：

```cpp
class IFileReader {
public:
    virtual bool open(const std::string& relativePath) = 0;
    virtual ssize_t readChunk(uint8_t* buffer, size_t size) = 0;
    virtual FileMetaData getMetaData() const = 0;
};
```

- `open()` 打开源目录下的相对路径文件
- `readChunk()` 按块读取（通常 4KB），返回实际读取字节数
- `getMetaData()` 返回文件元数据

## IFileWriter / FileSystemWriter

文件写入与元数据恢复：

```cpp
class IFileWriter {
public:
    virtual bool open(const std::string& relativePath) = 0;
    virtual bool writeChunk(const uint8_t* data, size_t size) = 0;
    virtual bool applyMetaData(const FileMetaData& meta) = 0;
    virtual void close() = 0;
};
```

- `open()` 在目标目录下创建相对路径文件（含父目录）
- `writeChunk()` 写入数据块
- `applyMetaData()` 恢复 POSIX 元数据（`chmod`, `chown`, `utime`）

## BackupEngine

备份调度引擎，编排完整的备份/恢复流程：

```cpp
BackupEngine engine;
engine.backupDirectory(sourceRoot, archivePath, archiveWriter, config, filterOptions);
// 或
engine.restoreArchive(archivePath, archiveReader, targetRoot, config);
```

### 备份流程

1. `DirectoryTraverser::scan()` 遍历源目录
2. 对每个条目：`FileSystemReader::open()` → `archiveWriter.addFile(reader)`
3. `archiveWriter.finalize()` 完成写入
4. 将临时文件重命名为最终归档路径

### 恢复流程

1. `archiveReader.open()` 打开归档
2. 循环：`getNextFileMeta()` → `FileSystemWriter::open()` → `readChunk()` → `writeChunk()` → `applyMetaData()`
3. `archiveReader.close()`

### 统计信息

```cpp
struct EngineStats {
    uint64_t filesProcessed;
    uint64_t directoriesProcessed;
    uint64_t bytesProcessed;
};
```

## 错误处理

- 遍历错误累积在 `DirectoryTraverser::errors()` 中
- I/O 错误通过 `BackupEngine::lastError()` 获取
- 备份失败时自动清理临时文件
- 异常安全的 RAII 资源管理

## 相关文档

- [系统架构](architecture.md)
- [数据管道模块](pipeline.md)
- [测试框架](testing.md)
