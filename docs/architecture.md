# 系统架构

> DataBackup — 基于 Linux 终端的高效、安全、自动化数据备份工具

## 模块总览

```
DataBackup/
├── include/           # 头文件（接口定义）
│   ├── common/        # 共享类型 (FileMetaData, BackupConfig)
│   ├── core/          # 核心引擎模块（文件遍历、过滤、读写）
│   ├── pipeline/      # 数据管道模块（打包、压缩、加密、归档）
│   └── monitor/       # 系统监控模块（inotify、Daemon）
├── src/               # 源文件（接口实现）
│   ├── core/
│   ├── pipeline/
│   └── main.cpp       # CLI 入口
├── tests/             # 单元测试（gtest）
├── docs/              # 项目文档
└── build/             # 构建输出
```

## 模块职责

| 模块 | 目录 | 职责 |
|---|---|---|
| 核心引擎 | `include/core/`, `src/core/` | 文件系统遍历、规则过滤、基础读写、元数据提取与恢复、备份调度 |
| 数据管道 | `include/pipeline/`, `src/pipeline/` | 多文件打包/解包、流式压缩/解压、流式加密/解密、密钥派生、二进制归档格式读写 |
| 系统监控 | `include/monitor/` | 基于 inotify 的文件系统事件监听、Daemon 守护进程、增量备份触发 |
| 测试 | `tests/` | Google Test 单元测试 (103 用例) |
| CLI | `src/main.cpp` | 命令行参数解析、backup/restore 子命令 |

## 数据流向

### 备份 (backup)

```
源目录
  │
  ▼
DirectoryTraverser::scan()      ← 递归遍历，应用 FileFilter 过滤器
  │
  ▼
[每个文件]
  │
  ├── FileSystemReader           ← 从磁盘读取文件内容
  │
  ▼
ArchiveWriterImpl
  │
  ├── [可选] StreamPacker        ← 多文件打包为连续字节流
  ├── [可选] StreamCompressor    ← RLE 流式压缩
  ├── [可选] StreamEncryptor     ← RC4 流式加密
  │
  ▼
归档文件 (.dbak)                   ← 自定义二进制格式
```

### 恢复 (restore)

```
归档文件 (.dbak)
  │
  ▼
ArchiveReaderImpl
  │
  ├── [可选] StreamEncryptor     ← RC4 流式解密
  ├── [可选] StreamCompressor    ← RLE 流式解压
  ├── [可选] StreamPacker        ← 从打包流中提取各文件
  │
  ▼
[每个文件]
  │
  ├── FileSystemWriter           ← 写入磁盘，恢复元数据
  │
  ▼
目标目录
```

## 二进制归档格式 (v3)

```
┌─────────────────────────────────────────┐
│ GlobalHeader (48 bytes)                 │
│   magic "DBAK"  version=3  flags        │
│   salt[16]  iv[16]  compressionLevel    │
├─────────────────────────────────────────┤
│ File entries...                         │
│   ┌───────────────────────────────────┐ │
│   │ FileEntryHeader (39 bytes)        │ │
│   │   pathLength  originalSize         │ │
│   │   permissions  owner  group        │ │
│   │   accessTime  modifyTime           │ │
│   ├───────────────────────────────────┤ │
│   │ path (pathLength bytes)           │ │
│   ├───────────────────────────────────┤ │
│   │ [chunkLen:4][compressed+encrypted] │ │  ← 4KB 块
│   │ [chunkLen:4][compressed+encrypted] │ │
│   │ ...                                │ │
│   │ [0:4] EOF marker                   │ │
│   └───────────────────────────────────┘ │
│   ... (more files or packed blob)       │
├─────────────────────────────────────────┤
│ ArchiveFooter (16 bytes)                │
│   indexOffset  fileCount  magic "KABD" │
└─────────────────────────────────────────┘
```

**打包模式 (FLAG_PACK)**: 所有文件合并为一个 `.packed` 条目，打包流格式为:
`[FileEntryHeader][path][raw data]` 按文件顺序拼接。

## 技术栈

| 层级 | 技术 |
|---|---|
| 语言 | C++17 |
| 构建 | CMake ≥ 3.14 |
| 压缩 | PackBits 风格 RLE（自研） |
| 加密 | RC4 兼容流密码（自研） |
| 密钥派生 | 迭代混合函数（自研） |
| 测试 | Google Test v1.14.0 (FetchContent) |
| 外部依赖 | **零**（仅需 C++ 标准库和 POSIX） |

## 相关文档

- [核心引擎模块](core-engine.md)
- [数据管道模块](pipeline.md)
- [后端算法说明](backend-algorithms.md)
- [系统监控模块](monitor.md)
- [测试框架与环境](testing.md)
- [加密用例规格](encrypt.md)
