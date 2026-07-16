# 项目代码导读

> 目标读者：第一次接触本项目、不了解代码结构的人。本文按“程序怎么启动、数据怎么流动、每个模块负责什么”来解释主要代码。

## 1. 项目做什么

DataBackup 是一个命令行数据备份工具，主要功能是：

- 把一个目录备份成 `.dbak` 归档文件。
- 从 `.dbak` 归档文件恢复目录。
- 可选启用压缩、打包、加密。
- 可选启用 `watch` 模式，监听目录变化并自动触发备份。

用户最终使用的是 `build/databackup` 这个可执行文件。

## 2. 目录结构

```text
DataBackup/
├── include/                 # 头文件：接口和数据结构
│   ├── common/              # 公共类型
│   ├── core/                # 核心引擎：遍历、过滤、文件读写、备份恢复编排
│   ├── pipeline/            # 数据管道：归档格式、压缩、加密、打包
│   └── monitor/             # 监控模块：inotify、watch 模式
├── src/                     # 源文件：具体实现
│   ├── core/
│   ├── pipeline/
│   ├── monitor/
│   └── main.cpp             # CLI 入口
├── tests/                   # 自动化测试
└── docs/                    # 项目文档
```

可以把项目理解成四层：

```text
CLI 命令行
   |
   v
BackupEngine 核心编排
   |
   v
FileSystemReader / FileSystemWriter + ArchiveReader / ArchiveWriter
   |
   v
压缩、加密、打包、归档格式
```

## 3. 最重要的公共数据结构

公共类型定义在：

```text
include/common/Types.h
```

### FileMetaData

`FileMetaData` 表示一个文件或目录的元数据。

它保存：

- 相对路径
- 文件权限
- 属主 ID
- 属组 ID
- 访问时间
- 修改时间
- 是否是目录
- 文件大小

备份时，核心模块从真实文件系统读取这些信息；归档模块把这些信息写进 `.dbak` 文件；恢复时，核心模块再用这些信息恢复文件权限和时间。

### BackupConfig

`BackupConfig` 表示一次备份或恢复的配置。

它控制：

- 是否启用压缩：`enableCompression`
- 是否启用加密：`enableEncryption`
- 是否启用打包：`enablePacking`
- 密码：`password`
- 压缩级别：`compressionLevel`

CLI 解析命令行参数后，会把参数转换成 `BackupConfig`，再交给底层模块。

## 4. 程序入口：main.cpp

入口文件是：

```text
src/main.cpp
```

它负责三件事：

1. 解析命令行参数。
2. 根据子命令调用对应功能。
3. 把用户输入转换成核心模块能理解的配置。

当前支持三个子命令：

```bash
databackup backup
databackup restore
databackup watch
```

### backup 命令

`runBackup()` 会：

1. 检查源目录是否存在。
2. 构造 `BackupConfig`。
3. 创建 `BackupEngine`。
4. 创建 `ArchiveWriterImpl`。
5. 调用 `BackupEngine::backupDirectory()`。

简化理解：

```cpp
BackupEngine engine;
ArchiveWriterImpl writer;
engine.backupDirectory(sourceRoot, archivePath, writer, config);
```

### restore 命令

`runRestore()` 会：

1. 检查归档文件是否存在。
2. 构造 `BackupConfig`。
3. 创建 `BackupEngine`。
4. 创建 `ArchiveReaderImpl`。
5. 调用 `BackupEngine::restoreArchive()`。

简化理解：

```cpp
BackupEngine engine;
ArchiveReaderImpl reader;
engine.restoreArchive(archivePath, reader, targetRoot, config);
```

### watch 命令

`runWatch()` 会：

1. 检查被监控目录是否存在。
2. 创建 `BackupEngine`。
3. 给引擎设置文件事件回调。
4. 创建 `Monitor`。
5. 开始监听目录变化。
6. 文件变化时触发一次备份。

也就是说，`watch` 模式不是只备份某个变化文件，而是在检测到变化后重新触发一次目录备份。

## 5. 核心引擎模块

核心引擎代码位于：

```text
include/core/
src/core/
```

它负责和真实文件系统打交道。

### BackupEngine

主要文件：

```text
include/core/BackupEngine.h
src/core/BackupEngine.cpp
```

`BackupEngine` 是项目的核心编排类。

它本身不负责压缩、加密，也不直接理解 `.dbak` 的二进制格式。它只负责把流程串起来。

备份流程：

```text
源目录
  |
  v
DirectoryTraverser 扫描目录
  |
  v
FileSystemReader 逐个读取文件
  |
  v
ArchiveWriterImpl 写入归档
```

恢复流程：

```text
.dbak 归档文件
  |
  v
ArchiveReaderImpl 读取归档
  |
  v
FileSystemWriter 写回目标目录
```

`BackupEngine` 还保存统计信息：

- 处理了多少文件
- 处理了多少目录
- 处理了多少字节

这些数据通过 `stats()` 获取。

### DirectoryTraverser

主要文件：

```text
include/core/DirectoryTraverser.h
src/core/DirectoryTraverser.cpp
```

`DirectoryTraverser` 用来递归扫描源目录。

它会：

- 遍历目录下的文件和子目录。
- 跳过符号链接。
- 读取每个文件或目录的元数据。
- 生成相对路径。
- 按相对路径排序。
- 应用 `FileFilter` 过滤规则。

它返回的是 `std::vector<FileMetaData>`。

### FileFilter

主要文件：

```text
include/core/FileFilter.h
src/core/FileFilter.cpp
```

`FileFilter` 判断一个文件或目录是否应该被备份。

支持：

- 排除 glob 模式，比如 `*.log`
- 只包含指定扩展名，比如 `.txt`、`.md`
- 文件大小范围
- 修改时间范围
- 属主 UID
- 是否包含隐藏文件
- 是否包含目录

### FileSystemReader

主要文件：

```text
include/core/FileSystemReader.h
src/core/FileSystemReader.cpp
```

`FileSystemReader` 是真实文件读取器。

它实现了接口：

```text
IFileReader
```

它的职责：

- 根据源目录和相对路径打开文件。
- 分块读取文件内容。
- 返回文件元数据。
- 防止路径逃逸，比如拒绝 `../xxx` 和绝对路径。

归档模块不直接打开真实文件，而是通过 `IFileReader` 读取数据。这样核心模块和归档模块之间保持解耦。

### FileSystemWriter

主要文件：

```text
include/core/FileSystemWriter.h
src/core/FileSystemWriter.cpp
```

`FileSystemWriter` 是真实文件写入器。

它实现了接口：

```text
IFileWriter
```

它的职责：

- 在恢复目录下创建文件。
- 自动创建父目录。
- 分块写入数据。
- 恢复权限、属主、属组和时间。
- 防止路径逃逸。

## 6. 数据管道模块

数据管道代码位于：

```text
include/pipeline/
src/pipeline/
```

它负责 `.dbak` 文件格式、压缩、加密和打包。

### ArchiveFormat

主要文件：

```text
include/pipeline/ArchiveFormat.h
```

这个文件定义 `.dbak` 的二进制格式。

主要结构有：

- `ArchiveGlobalHeader`：整个归档文件的头部。
- `FileEntryHeader`：每个文件或目录的头部。
- `ArchiveFooter`：归档文件尾部。

全局头部里保存：

- 魔数 `DBAK`
- 格式版本号
- 是否压缩
- 是否加密
- 是否打包
- 加密 salt
- 加密 IV
- 压缩级别

### ArchiveWriterImpl

主要文件：

```text
include/pipeline/ArchiveWriterImpl.h
src/pipeline/ArchiveWriterImpl.cpp
```

`ArchiveWriterImpl` 负责写 `.dbak` 文件。

它实现接口：

```text
IArchiveWriter
```

备份时，`BackupEngine` 会对每个文件调用：

```cpp
archiveWriter.addFile(reader);
```

`ArchiveWriterImpl` 内部会：

1. 写入全局头。
2. 写入文件头。
3. 从 `IFileReader` 分块读取文件内容。
4. 可选压缩。
5. 可选加密。
6. 写入 chunk 数据。
7. 最后写入 footer。

### ArchiveReaderImpl

主要文件：

```text
include/pipeline/ArchiveReaderImpl.h
src/pipeline/ArchiveReaderImpl.cpp
```

`ArchiveReaderImpl` 负责读 `.dbak` 文件。

它实现接口：

```text
IArchiveReader
```

恢复时，`BackupEngine` 会循环调用：

```cpp
archiveReader.getNextFileMeta(meta);
archiveReader.readChunk(buffer, size);
```

`ArchiveReaderImpl` 内部会：

1. 读取并校验全局头。
2. 判断是否压缩、加密、打包。
3. 如果加密，使用密码派生密钥。
4. 逐个读取文件元数据。
5. 分块读取数据。
6. 可选解密。
7. 可选解压。
8. 如果是打包模式，从打包流中拆出真实文件。

### StreamCompressor

主要文件：

```text
include/pipeline/StreamCompressor.h
src/pipeline/StreamCompressor.cpp
```

`StreamCompressor` 实现 RLE 压缩。

RLE 的核心思想是：

```text
如果有很多连续相同的字节，就把它表示成“重复多少次 + 重复的值”。
```

例如：

```text
AAAAAAAAAA
```

可以被压缩成类似：

```text
10 个 A
```

它适合压缩重复内容较多的数据。

### StreamEncryptor

主要文件：

```text
include/pipeline/StreamEncryptor.h
src/pipeline/StreamEncryptor.cpp
```

`StreamEncryptor` 实现 RC4 风格的流加密。

流加密可以理解为：

```text
生成一串伪随机字节流，然后和原始数据逐字节 XOR。
```

加密和解密使用同一个过程：

```text
密文 = 明文 XOR 密钥流
明文 = 密文 XOR 密钥流
```

### KeyDerivation

主要文件：

```text
include/pipeline/KeyDerivation.h
src/pipeline/KeyDerivation.cpp
```

`KeyDerivation` 用来把用户输入的密码变成固定长度的二进制密钥。

它还负责：

- 生成随机 salt 和 IV。
- 安全清空内存中的密钥数据。

为什么不直接把密码当密钥？

因为用户密码通常长度不固定、随机性不够。密钥派生可以把密码转换成更适合加密算法使用的字节序列。

### StreamPacker

主要文件：

```text
include/pipeline/StreamPacker.h
src/pipeline/StreamPacker.cpp
```

`StreamPacker` 用来把多个文件打包成一个连续数据流。

打包前：

```text
file1.txt
file2.txt
docs/readme.md
```

打包后：

```text
[file1 header][file1 data][file2 header][file2 data][readme header][readme data]
```

这样做的好处是：

- 管道可以把多个文件作为一个整体处理。
- 压缩和加密可以作用在连续数据流上。

## 7. 监控模块

监控模块代码位于：

```text
include/monitor/
src/monitor/
```

它负责 `watch` 模式。

### FileEvent

主要文件：

```text
include/monitor/FileEvent.h
```

`FileEvent` 表示一个文件系统事件。

它保存：

- 文件路径
- 事件类型
- 是否目录
- 时间戳

事件类型包括：

- 创建
- 修改
- 删除
- 移出
- 移入

### IFileEventListener

主要文件：

```text
include/monitor/IFileEventListener.h
```

这是事件监听接口。

只要一个类实现了：

```cpp
void onFileEvent(const FileEvent& event);
```

就可以接收文件变化事件。

`BackupEngine` 实现了这个接口，所以 `Monitor` 可以把文件变化通知给 `BackupEngine`。

### Monitor

主要文件：

```text
include/monitor/Monitor.h
src/monitor/Monitor.cpp
```

`Monitor` 基于 Linux inotify 实现目录监听。

它会：

- 给源目录和子目录添加 watch。
- 在后台线程读取 inotify 事件。
- 把底层事件转换成 `FileEvent`。
- 做简单防抖，避免短时间重复触发。
- 调用 listener 的 `onFileEvent()`。

在 `watch` 命令里，listener 就是 `BackupEngine`。

## 8. 自动化测试

测试代码位于：

```text
tests/
```

主要测试文件：

- `test_file_filter.cpp`
- `test_core_filesystem.cpp`
- `test_archive_format.cpp`
- `test_archive_impl.cpp`
- `test_backup_engine.cpp`
- `test_stream_compressor.cpp`
- `test_stream_encryptor.cpp`
- `test_key_derivation.cpp`
- `test_stream_packer.cpp`
- `MonitorTests.cpp`

运行测试：

```bash
ctest --test-dir build --output-on-failure
```

测试的作用是验证：

- 过滤规则是否正确。
- 文件读写是否安全。
- 归档格式是否正确。
- 压缩、加密、打包是否能往返。
- 备份和恢复后内容是否一致。
- Monitor 是否能收到文件创建事件。

## 9. 一次完整备份的调用链

用户执行：

```bash
./build/databackup backup -s /tmp/source -d /tmp/out.dbak --compress --pack --encrypt --password 123456
```

代码调用链：

```text
main.cpp
  |
  v
runBackup()
  |
  v
BackupEngine::backupDirectory()
  |
  v
DirectoryTraverser::scan()
  |
  v
FileSystemReader::open()
  |
  v
ArchiveWriterImpl::addFile()
  |
  v
StreamPacker / StreamCompressor / StreamEncryptor
  |
  v
写入 .dbak 文件
```

## 10. 一次完整恢复的调用链

用户执行：

```bash
./build/databackup restore -s /tmp/out.dbak -d /tmp/restore --password 123456
```

代码调用链：

```text
main.cpp
  |
  v
runRestore()
  |
  v
BackupEngine::restoreArchive()
  |
  v
ArchiveReaderImpl::open()
  |
  v
ArchiveReaderImpl::getNextFileMeta()
  |
  v
ArchiveReaderImpl::readChunk()
  |
  v
StreamEncryptor / StreamCompressor / StreamPacker
  |
  v
FileSystemWriter::writeChunk()
  |
  v
恢复文件和元数据
```

## 11. 一次 watch 自动备份的调用链

用户执行：

```bash
./build/databackup watch -s /tmp/source -d /tmp/watch.dbak --compress --pack
```

代码调用链：

```text
main.cpp
  |
  v
runWatch()
  |
  v
Monitor::start()
  |
  v
Monitor::watchLoop()
  |
  v
检测到 inotify 事件
  |
  v
BackupEngine::onFileEvent()
  |
  v
增量回调触发 backupDirectory()
  |
  v
生成新的 .dbak 归档
```

## 12. 模块之间为什么这样设计

项目使用接口隔离不同职责。

例如：

- `BackupEngine` 不直接知道归档文件怎么写，只依赖 `IArchiveWriter`。
- `ArchiveWriterImpl` 不直接知道文件系统怎么读，只依赖 `IFileReader`。
- `ArchiveReaderImpl` 不直接知道恢复目录怎么写，只把数据提供给 `BackupEngine`。
- `Monitor` 不直接执行备份，只通知 `IFileEventListener`。

这样做的好处：

- 每个模块职责清晰。
- 测试时可以单独测一个模块。
- 后续可以替换实现，比如换压缩算法、换归档格式、换 GUI 入口。

## 13. 新人读代码建议顺序

建议按下面顺序读：

1. `include/common/Types.h`
2. `src/main.cpp`
3. `include/core/BackupEngine.h`
4. `src/core/BackupEngine.cpp`
5. `src/core/DirectoryTraverser.cpp`
6. `src/core/FileSystemReader.cpp`
7. `src/core/FileSystemWriter.cpp`
8. `include/pipeline/ArchiveFormat.h`
9. `src/pipeline/ArchiveWriterImpl.cpp`
10. `src/pipeline/ArchiveReaderImpl.cpp`
11. `src/pipeline/StreamCompressor.cpp`
12. `src/pipeline/StreamEncryptor.cpp`
13. `src/pipeline/StreamPacker.cpp`
14. `src/monitor/Monitor.cpp`
15. `tests/`

读完这些文件，就能理解项目的主要执行路径。

## 14. 一句话总结

这个项目的核心思想是：

```text
核心引擎负责“找文件、读文件、写文件”，数据管道负责“把数据变成安全的归档文件”，监控模块负责“发现文件变化”，CLI 负责“把用户命令转换成模块调用”。
```
