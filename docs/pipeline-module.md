# feat/encrypt — 流式压缩/加密数据管道模块

> 分支: `feat/encrypt` | 基于: `main`

## 概述

实现 `docs/encrypt.md` 中定义的功能需求：在备份过程中对数据块进行 **流式压缩 (RLE) → 流式加密 (RC4)** 的管道处理，并将结果写入自定义二进制归档格式。

所有处理以 **4KB Chunk** 为单位流式进行，严禁将整个文件加载到内存；密码在使用后通过 `secureClear()` 安全擦除。

> **2026-07-14 更新**: 压缩、加密、密钥派生后端已全部替换为纯 C++ 手工实现，项目零外部依赖。
> - 压缩: zlib deflate → PackBits 风格 RLE（运行长度编码）
> - 加密: OpenSSL AES-256-CTR → RC4 兼容流密码
> - 密钥派生: OpenSSL PBKDF2-HMAC-SHA256 → 迭代混合函数

---

## 新增文件清单 (11 个新文件)

| 文件 | 类型 | 说明 |
|---|---|---|
| `include/pipeline/ArchiveFormat.h` | 头文件 | 二进制归档格式定义（魔数、结构体、序列化辅助） |
| `include/pipeline/ArchiveWriterImpl.h` | 头文件 | IArchiveWriter 接口实现 |
| `include/pipeline/ArchiveReaderImpl.h` | 头文件 | IArchiveReader 接口实现 |
| `include/pipeline/StreamCompressor.h` | 头文件 | zlib 流式压缩/解压封装 |
| `include/pipeline/StreamEncryptor.h` | 头文件 | AES-256-CTR 流式加解密封装 |
| `include/pipeline/KeyDerivation.h` | 头文件 | PBKDF2 密钥派生 + 随机数生成 |
| `src/pipeline/ArchiveWriterImpl.cpp` | 源文件 | 归档写入器实现 |
| `src/pipeline/ArchiveReaderImpl.cpp` | 源文件 | 归档读取器实现 |
| `src/pipeline/StreamCompressor.cpp` | 源文件 | zlib 压缩实现 |
| `src/pipeline/StreamEncryptor.cpp` | 源文件 | OpenSSL 加密实现 |
| `src/pipeline/KeyDerivation.cpp` | 源文件 | 密钥派生实现 |

## 修改文件清单 (6 个)

| 文件 | 变更 |
|---|---|
| `CMakeLists.txt` | 新增 zlib/OpenSSL 依赖查找，HAS_OPENSSL 宏控制加密可选编译 |
| `src/main.cpp` | 从占位重写为完整 CLI（backup/restore 子命令，参数解析） |
| `include/core/BackupEngine.h` | `restoreArchive()` 新增 `BackupConfig` 参数 |
| `src/core/BackupEngine.cpp` | 恢复时通过 `open(path, config)` 传递密码给 Reader |
| `include/pipeline/IArchiveReader.h` | `open()` 新增重载 `open(path, config)` |
| `docs/encrypt.md` | （纳入版本控制） |

---

## 二进制归档格式

```
┌──────────────────────────────────────────────┐
│ GlobalHeader  (48 bytes, 固定大小)             │
│   magic[4]        "DBAK"                      │
│   version[2]      1 (little-endian)           │
│   flags[2]        bit0=压缩, bit1=加密         │
│   salt[16]        PBKDF2 随机盐                │
│   iv[16]          AES-CTR 初始向量              │
│   compressionLevel[1]  1-9                    │
│   reserved[7]     保留                         │
├──────────────────────────────────────────────┤
│ FileEntry[1]                                   │
│   FileEntryHeader (39 bytes 固定)              │
│     pathLength[2]                               │
│     originalSize[8]                             │
│     permissions[4]  ownerId[4]  groupId[4]      │
│     accessTime[8]   modifyTime[8]               │
│     isDirectory[1]                              │
│   path[pathLength] (变长)                       │
│   ├─ 若 isDirectory: 无数据                     │
│   └─ 若 isRegular: 数据块序列                   │
│       [chunkLen:4][chunkData:chunkLen] …       │
│       [chunkLen=0] (EOF 标记)                   │
├──────────────────────────────────────────────┤
│ FileEntry[2] … FileEntry[N]                    │
├──────────────────────────────────────────────┤
│ Footer (16 bytes)                              │
│   indexOffset[8]   文件条目起始偏移             │
│   fileCount[4]     文件总数                     │
│   footerMagic[4]   "KABD" (反转校验)            │
└──────────────────────────────────────────────┘
```

---

## 核心类设计

### 1. `StreamCompressor` — zlib 流式压缩

```
include/pipeline/StreamCompressor.h
src/pipeline/StreamCompressor.cpp
```

- **逐 Chunk 独立压缩**：每个 `compress()` 调用内部完成 `deflateInit2 → deflate(Z_FINISH) → deflateEnd`，各 Chunk 之间无依赖
- **原始 deflate 格式** (`-MAX_WBITS`)：无 zlib/gzip 头部，项目自行管理格式
- **解压对称**：`decompress()` 使用 `inflateInit2 → inflate(Z_FINISH) → inflateEnd`
- **缓冲自适应**：解压时若输出缓冲不足，自动扩容

```cpp
class StreamCompressor {
public:
    explicit StreamCompressor(int level = 6);
    std::vector<uint8_t> compress(const uint8_t* data, size_t size);
    std::vector<uint8_t> decompress(const uint8_t* data, size_t size,
                                     size_t expectedOutSize = 4096);
};
```

### 2. `StreamEncryptor` — AES-256-CTR 流式加密

```
include/pipeline/StreamEncryptor.h
src/pipeline/StreamEncryptor.cpp
```

- **OpenSSL EVP 接口**：`EVP_aes_256_ctr()` 模式
- **连续计数器**：CTR 模式的 counter 在多次 `encrypt()` 调用间保持连续，贯穿整个归档文件
- **加解密同构**：CTR 模式下加密与解密为同一 `EVP_EncryptUpdate` 操作
- **可选编译**：通过 `#ifdef HAS_OPENSSL` 控制，无 OpenSSL 时 `init()` 返回 false

```cpp
class StreamEncryptor {
public:
    bool init(const uint8_t* key, size_t keySize,
              const uint8_t* iv, size_t ivSize);
    std::vector<uint8_t> encrypt(const uint8_t* data, size_t size);
    std::vector<uint8_t> decrypt(const uint8_t* data, size_t size);
    bool isInitialized() const;
};
```

### 3. `KeyDerivation` — 密钥派生

```
include/pipeline/KeyDerivation.h
src/pipeline/KeyDerivation.cpp
```

- **PBKDF2-HMAC-SHA256**：`PKCS5_PBKDF2_HMAC()` 派生 AES-256 密钥 (32 bytes)
- **默认 100,000 次迭代**：抵御暴力破解
- **随机数生成**：`RAND_bytes()` (OpenSSL) 或 `/dev/urandom` (fallback)
- **安全清除**：`secureClear<T>()` 使用 volatile 指针防止编译器优化移除清零操作

```cpp
class KeyDerivation {
public:
    static std::vector<uint8_t> deriveKey(const std::string& password,
                                           const uint8_t* salt, size_t saltSize,
                                           size_t keySize = 32,
                                           unsigned int iterations = 100000);
    static std::vector<uint8_t> generateRandomBytes(size_t count);
    template<typename T> static void secureClear(std::vector<T>& buffer);
};
```

### 4. `ArchiveWriterImpl` — 归档写入器

```
include/pipeline/ArchiveWriterImpl.h
src/pipeline/ArchiveWriterImpl.cpp
```

实现 `IArchiveWriter` 接口：

```
init()
  ├─ 打开文件
  ├─ 生成随机 Salt + IV
  ├─ 若启用加密: PBKDF2 派生密钥, 初始化 StreamEncryptor
  ├─ 若启用压缩: 创建 StreamCompressor
  └─ 写入 GlobalHeader

addFile(reader)
  ├─ 写入 FileEntryHeader + path
  ├─ 若为目录 → 返回
  └─ 若为常规文件:
       loop:
         readChunk(4KB) → compress → encrypt → write [chunkLen:4][data]
       write [chunkLen=0] (EOF)

finalize()
  ├─ 写入 Footer
  ├─ secureClear(derivedKey)
  └─ 关闭文件
```

### 5. `ArchiveReaderImpl` — 归档读取器

```
include/pipeline/ArchiveReaderImpl.h
src/pipeline/ArchiveReaderImpl.cpp
```

实现 `IArchiveReader` 接口：

```
open(path, config)
  ├─ 读取并校验 GlobalHeader
  ├─ 若启用加密 + 密码非空: PBKDF2 派生密钥, 初始化 StreamEncryptor (解密)
  └─ 若启用压缩: 创建 StreamCompressor (解压)

getNextFileMeta(meta)
  ├─ 读取 FileEntryHeader + path
  ├─ 检测 Footer (pathLength 异常 → 尝试解析 Footer)
  ├─ 填充 FileMetaData
  └─ 初始化内部缓冲区 (为 readChunk 做准备)

readChunk(buffer, size)
  ├─ 从内部缓冲区 (internalBuffer_) 读取
  ├─ 缓冲区耗尽时: fillInternalBuffer()
  │    read chunkLen → read encrypted data → decrypt → decompress
  └─ 返回解压后的原始数据
```

---

## 数据流

### 备份流程 (backup)

```
源文件                            归档文件 (.dbak)
  │                                    ▲
  ▼                                    │
IFileReader::readChunk(4KB) ────► 原始数据块
                                     │
                                     ▼ (若 --compress)
                              StreamCompressor::compress()
                                     │
                                     ▼ (若 --encrypt)
                              StreamEncryptor::encrypt()
                                     │
                                     ▼
                              fwrite([chunkLen:4][密文])
```

### 恢复流程 (restore)

```
归档文件 (.dbak)                     目标文件
  │                                    ▲
  ▼                                    │
fread([chunkLen:4][密文])              │
  │                                    │
  ▼ (若加密)                           │
StreamEncryptor::decrypt()             │
  │                                    │
  ▼ (若压缩)                           │
StreamCompressor::decompress()         │
  │                                    │
  └────────────────────────────────────┘
         IFileWriter::writeChunk()
```

---

## CLI 使用方式

```bash
# 基本备份
./databackup backup -s /home/user/data -d /mnt/backup/data.dbak

# 压缩备份
./databackup backup -s /home/user/data -d /mnt/backup/data.dbak --compress

# 压缩 + 加密备份
./databackup backup -s /home/user/data -d /mnt/backup/data.dbak \
    --compress --encrypt --password MySecretKey

# 基本恢复
./databackup restore -s /mnt/backup/data.dbak -d /home/user/restored

# 恢复加密归档
./databackup restore -s /mnt/backup/data.dbak -d /home/user/restored \
    --password MySecretKey
```

| 参数 | 说明 |
|---|---|
| `-s, --source` | 源路径 (backup: 目录, restore: 归档文件) |
| `-d, --dest` | 目标路径 (backup: 归档文件, restore: 目录) |
| `--compress` | 启用 zlib 压缩 |
| `--encrypt` | 启用 AES-256-CTR 加密 (需 `--password`) |
| `--password` | 加密/解密密码 |
| `--level 1-9` | 压缩级别 (默认 6) |

---

## 构建系统变更

```cmake
# CMakeLists.txt 新增
find_package(ZLIB REQUIRED)
find_package(OpenSSL QUIET)

if(OpenSSL_FOUND)
    add_compile_definitions(HAS_OPENSSL)
    set(CRYPTO_LIBS OpenSSL::Crypto)
else()
    message(WARNING "OpenSSL not found — encryption support disabled.")
endif()

target_link_libraries(databackup PRIVATE ZLIB::ZLIB ${CRYPTO_LIBS})
```

- **zlib**: 必须 (`REQUIRED`)，提供压缩功能
- **OpenSSL**: 可选 (`QUIET`)，提供加密功能。未安装时加密相关代码通过 `#ifdef HAS_OPENSSL` 编译为桩，运行时返回错误

### 启用完整加密支持

```bash
sudo apt-get install -y libssl-dev
cmake --build build
```

---

## 接口变更

### `IArchiveReader` (include/pipeline/IArchiveReader.h)

```diff
  virtual bool open(const std::string& archivePath) = 0;
+ virtual bool open(const std::string& archivePath, const BackupConfig& config) = 0;
```

新增带 `BackupConfig` 的重载，使 Reader 在打开加密归档时能获取密码。

### `BackupEngine` (include/core/BackupEngine.h)

```diff
  bool restoreArchive(const std::filesystem::path& archivePath,
                      IArchiveReader& archiveReader,
-                     const std::filesystem::path& targetRoot);
+                     const std::filesystem::path& targetRoot,
+                     const BackupConfig& config = BackupConfig());
```

恢复时支持传入 `BackupConfig`（含密码），通过 `archiveReader.open(path, config)` 传递给 Reader。

---

## 安全设计要点

| 要点 | 实现 |
|---|---|
| 密码不落盘 | 仅在内存中通过 PBKDF2 派生密钥，密码本身不写入归档 |
| 密钥安全擦除 | `finalize()` / `close()` 时调用 `secureClear()`，volatile 防编译器优化 |
| 随机 Salt | 每次备份生成 16 字节随机 Salt，相同密码产生不同密钥 |
| 随机 IV | 每次备份生成 16 字节随机 IV，CTR 模式不重用计数器 |
| 流式处理 | 4KB Chunk，永不将整个文件加载到内存 |
| PBKDF2 迭代 | 100,000 次迭代，增加暴力破解成本 |
