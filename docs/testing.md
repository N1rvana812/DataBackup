# 测试框架与环境

> 测试框架: Google Test (gtest) v1.14.0 | CTest | 分支: `feat/encrypt` | 提交: `03bdd59`

## 概述

项目引入 Google Test 作为单元测试框架，通过 CMake `FetchContent` 自动下载依赖，无需手动安装。测试覆盖核心引擎（FileFilter、DirectoryTraverser）、数据管道（StreamCompressor、StreamEncryptor、KeyDerivation）和归档格式（ArchiveFormat）等模块。

测试通过 CTest 运行，支持 `ctest --output-on-failure` 查看失败详情，输出兼容 IDE 和 CI 集成。

---

## 测试环境

### 依赖

| 组件 | 版本 | 用途 |
|---|---|---|
| Google Test | v1.14.0 | 单元测试框架，通过 FetchContent 自动下载 |
| CTest | CMake 3.22+ | 测试运行器，随 CMake 附带 |
| zlib | 1.2.11+ | StreamCompressor 测试所需 |
| OpenSSL (libssl-dev) | 3.0+ | StreamEncryptor / KeyDerivation 测试所需（可选） |

### 编译要求

- **编译器**: GCC 11+ 或 Clang 14+，支持 C++17
- **CMake**: ≥ 3.14
- **操作系统**: Linux

### 环境准备

```bash
# 安装编译依赖
sudo apt-get install -y build-essential cmake zlib1g-dev libssl-dev

# 克隆项目
git clone https://github.com/N1rvana812/DataBackup.git
cd DataBackup
```

---

## 构建与运行

### 配置（首次）

```bash
mkdir build && cd build
cmake .. -DBUILD_TESTS=ON
```

CMake 选项：

| 选项 | 默认值 | 说明 |
|---|---|---|
| `BUILD_TESTS` | `ON` | 启用单元测试编译，设为 `OFF` 跳过测试 |

CMake 会自动通过 FetchContent 从 GitHub 下载 Google Test v1.14.0，编译为静态库后链接到测试可执行文件。下载仅需首次执行，后续构建复用缓存。

### 编译

```bash
cmake --build build -j$(nproc)
```

编译产物：

| 产物 | 路径 | 说明 |
|---|---|---|
| `libdatabackup_lib.a` | `build/` | 项目静态库（不含 main.cpp） |
| `databackup` | `build/` | 主可执行文件 |
| `databackup_tests` | `build/tests/` | 测试可执行文件 |

### 运行测试

```bash
# 方式一：CTest（推荐）
ctest --output-on-failure

# 方式二：直接运行测试可执行文件
./build/tests/databackup_tests

# 方式三：按模块过滤
./build/tests/databackup_tests --gtest_filter="FileFilterTest.*"

# 方式四：列出所有测试
./build/tests/databackup_tests --gtest_list_tests
```

### 常规开发流程

```bash
# 一键：配置 → 编译 → 测试
cmake -S . -B build -DBUILD_TESTS=ON
cmake --build build -j$(nproc)
ctest --output-on-failure
```

---

## 测试结构

```
tests/
├── CMakeLists.txt                # 测试构建配置（FetchContent gtest）
├── test_file_filter.cpp          # FileFilter 测试（16 用例）
├── test_archive_format.cpp       # ArchiveFormat 测试（18 用例）
├── test_stream_compressor.cpp    # StreamCompressor 测试（17 用例）
├── test_stream_encryptor.cpp     # StreamEncryptor 测试（14 用例，条件编译）
└── test_key_derivation.cpp       # KeyDerivation 测试（16 用例，条件编译）
```

### 条件编译

`test_stream_encryptor.cpp` 和 `test_key_derivation.cpp` 仅在 OpenSSL 可用时编译。若未安装 `libssl-dev`，CMake 输出警告并跳过这两个文件。

CMake 配置阶段的输出：

```
# OpenSSL 已安装
-- OpenSSL found — encryption support enabled

# OpenSSL 未安装
-- OpenSSL not found — encryption support disabled.
   Install libssl-dev for AES encryption support.
```

---

## 测试覆盖总览

| 模块 | 测试文件 | 用例数 | 覆盖内容 |
|---|---|---|---|
| FileFilter | `test_file_filter.cpp` | 16 | 默认行为、扩展名过滤、隐藏文件、文件大小范围、glob 排除模式、组合过滤 |
| ArchiveFormat | `test_archive_format.cpp` | 18 | GlobalHeader 初始化/校验、魔数验证、Footer 序列化、FileEntry ↔ FileMetaData 往返转换、结构体大小断言 |
| StreamCompressor | `test_stream_compressor.cpp` | 17 | 压缩解压往返、压缩级别 (1-9)、高/低可压缩数据、空数据/null 边界、二进制保真、移动语义 |
| StreamEncryptor | `test_stream_encryptor.cpp` | 14 | 初始化验证、AES-256-CTR 加解密往返、流式分块处理、确定性/唯一性、未初始化异常、移动语义 |
| KeyDerivation | `test_key_derivation.cpp` | 16 | PBKDF2 密钥派生（正确大小、确定性、差异输入）、随机字节生成、`secureClear` 安全清零、集成流程 |

### 未覆盖范围

| 模块 | 原因 |
|---|---|
| `DirectoryTraverser` | 依赖真实文件系统，需集成测试环境（临时目录） |
| `FileSystemReader` / `FileSystemWriter` | 依赖真实文件 I/O，需集成测试环境 |
| `ArchiveReaderImpl` / `ArchiveWriterImpl` | 依赖完整的归档文件读写流程 |
| `BackupEngine` | 编排层，依赖上述所有模块 |
| `IMonitor` / inotify | 依赖内核事件和 Daemon 环境 |

---

## 架构说明

### CMake 构建层级

```
CMakeLists.txt (根)
├── add_library(databackup_lib STATIC ...)     ← 源文件（除 main.cpp）编译为静态库
├── add_executable(databackup src/main.cpp)    ← 主程序链接静态库
└── add_subdirectory(tests)                     ← BUILD_TESTS=ON 时引入
    └── tests/CMakeLists.txt
        ├── FetchContent(googletest v1.14.0)    ← 自动下载 gtest
        ├── add_executable(databackup_tests)    ← 测试可执行文件
        ├── target_link_libraries(... gtest_main gtest databackup_lib)
        └── gtest_discover_tests(...)            ← 自动注册到 CTest
```

### 静态库复用

为避免重复编译，源文件（除 `main.cpp`）编译为 `libdatabackup_lib.a` 静态库，主程序和测试共享链接：

```
src/*.cpp (excl. main.cpp)
       │
       ▼
libdatabackup_lib.a
       │
       ├── databackup (可执行文件)
       │       └── src/main.cpp
       │
       └── databackup_tests (测试)
               ├── test_file_filter.cpp
               ├── test_archive_format.cpp
               ├── test_stream_compressor.cpp
               ├── test_stream_encryptor.cpp
               └── test_key_derivation.cpp
```

---

## 编写新测试

### 基本结构

```cpp
#include "path/to/Header.h"
#include <gtest/gtest.h>

namespace backup {
namespace {

TEST(ModuleNameTest, ScenarioName) {
    // Arrange: 准备测试数据
    // Act: 调用被测接口
    // Assert: 验证结果
    EXPECT_EQ(result, expected);
}

}  // namespace
}  // namespace backup
```

### 常用断言

| 断言 | 说明 |
|---|---|
| `EXPECT_EQ(a, b)` / `ASSERT_EQ(a, b)` | a == b |
| `EXPECT_NE(a, b)` / `ASSERT_NE(a, b)` | a != b |
| `EXPECT_TRUE(cond)` / `ASSERT_TRUE(cond)` | cond 为 true |
| `EXPECT_FALSE(cond)` / `ASSERT_FALSE(cond)` | cond 为 false |
| `EXPECT_LT(a, b)` / `EXPECT_LE(a, b)` | a < b / a <= b |
| `EXPECT_STREQ(a, b)` | C 字符串相等 |
| `EXPECT_THROW(stmt, exc_type)` | stmt 抛出 exc_type 异常 |

`EXPECT_*`：失败后继续运行。`ASSERT_*`：失败后立即终止当前测试。

### 添加新测试文件

1. 在 `tests/` 下创建 `test_xxx.cpp`
2. 在 `tests/CMakeLists.txt` 的 `TEST_SOURCES` 列表中添加文件名
3. 重新配置编译：

```bash
cmake -S . -B build -DBUILD_TESTS=ON
cmake --build build -j$(nproc)
ctest --output-on-failure
```

---

## 变更历史

| 日期 | 提交 | 变更 |
|---|---|---|
| 2026-07-14 | `03bdd59` | 引入 gtest 框架，编写 81 个基础单元测试 |
