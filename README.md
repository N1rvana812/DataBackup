# DataBackup

> 基于 Linux 终端的高效、安全、自动化数据备份工具。

DataBackup 可以把一个目录备份为 `.dbak` 归档文件，也可以从 `.dbak` 文件恢复目录。当前版本支持基础备份恢复、打包、RLE 压缩、RC4 流加密、自定义文件筛选、元数据保存恢复，以及基于 inotify 的实时监控备份。

## 当前功能

- 目录备份：递归扫描源目录并生成 `.dbak` 归档文件。
- 目录恢复：从 `.dbak` 归档恢复文件和目录。
- 打包解包：通过 `--pack` 将多文件内容打包进统一归档数据流。
- 压缩解压：通过 `--compress` 启用 RLE 压缩，支持 `--level 1-9`。
- 加密解密：通过 `--encrypt --password <pwd>` 启用 RC4 流加密和密钥派生。
- 元数据处理：保存并尽量恢复权限、属主、属组、访问时间、修改时间等 POSIX 元数据。
- 自定义备份：支持 glob 排除、扩展名、文件大小、修改时间、属主 UID、隐藏文件、目录条目筛选。
- 实时备份：`watch` 模式监听目录变化并自动触发备份。
- Daemon 模式：`watch --daemon` 支持后台运行，并生成 PID 文件和日志文件。
- 自动化测试：使用 Google Test 和 CTest 覆盖核心引擎、数据管道、Monitor 和 CLI 冒烟测试。

目前暂未实现 GUI 图形界面、定时备份、数据淘汰策略；符号链接、管道、设备文件等特殊文件不会作为特殊文件完整备份，符号链接会被扫描阶段跳过。

## 环境要求

- Linux
- CMake 3.14+
- 支持 C++17 的编译器，例如 `g++`
- 构建测试时需要网络下载 Google Test，或使用已经缓存的依赖

项目运行时不依赖外部压缩或加密库；压缩、加密、打包和归档格式均由项目代码实现。

## 编译

只编译主程序：

```bash
cmake -S . -B build -DBUILD_TESTS=OFF
cmake --build build -j$(nproc)
```

编译主程序和测试：

```bash
cmake -S . -B build -DBUILD_TESTS=ON
cmake --build build -j$(nproc)
```

编译完成后，主程序位于：

```bash
./build/databackup
```

查看命令帮助：

```bash
./build/databackup --help
```

## 运行测试

如果配置时启用了 `-DBUILD_TESTS=ON`，可以执行：

```bash
ctest --test-dir build --output-on-failure
```

也可以直接运行 gtest 可执行文件：

```bash
./build/tests/databackup_tests
```

测试说明见 [docs/testing.md](docs/testing.md)。

## 基础使用

备份目录：

```bash
./build/databackup backup \
  -s /home/user/data \
  -d /mnt/backup/data.dbak
```

恢复归档：

```bash
./build/databackup restore \
  -s /mnt/backup/data.dbak \
  -d /home/user/restored
```

验证恢复结果：

```bash
diff -r /home/user/data /home/user/restored
```

## 打包、压缩和加密

创建打包、压缩、加密备份：

```bash
./build/databackup backup \
  -s /home/user/data \
  -d /mnt/backup/secure.dbak \
  --pack --compress --encrypt --password "MySecretKey"
```

恢复加密归档时必须提供密码：

```bash
./build/databackup restore \
  -s /mnt/backup/secure.dbak \
  -d /home/user/secure_restored \
  --password "MySecretKey"
```

压缩级别默认为 `6`，可通过 `--level` 指定：

```bash
./build/databackup backup \
  -s /home/user/data \
  -d /mnt/backup/compressed.dbak \
  --compress --level 9
```

## 自定义备份

当前支持的筛选参数：

| 参数 | 作用 |
| --- | --- |
| `--exclude <pattern>` | 按 glob 模式排除路径或文件名，可重复使用 |
| `--include-ext <ext>` | 只包含指定扩展名，例如 `.txt`，可重复使用 |
| `--min-size <bytes>` | 只包含不小于指定字节数的文件 |
| `--max-size <bytes>` | 只包含不大于指定字节数的文件 |
| `--modified-after <ts>` | 只包含修改时间不早于 Unix 时间戳的文件 |
| `--modified-before <ts>` | 只包含修改时间不晚于 Unix 时间戳的文件 |
| `--owner <uid>` | 只包含指定数字 UID 拥有的文件 |
| `--exclude-hidden` | 排除隐藏文件和隐藏目录 |
| `--exclude-dirs` | 不把目录条目写入归档 |

示例：只备份当前用户拥有的 `.txt` / `.md` 文件，排除日志、隐藏文件和过旧文件：

```bash
AFTER_TS="$(date -d '2020-01-01 00:00:00' +%s)"
CURRENT_UID="$(id -u)"

./build/databackup backup \
  -s /home/user/data \
  -d /mnt/backup/custom.dbak \
  --include-ext .txt \
  --include-ext .md \
  --exclude "*.log" \
  --exclude-hidden \
  --exclude-dirs \
  --min-size 1 \
  --modified-after "$AFTER_TS" \
  --owner "$CURRENT_UID"
```

## 实时监控备份

前台运行监控备份：

```bash
./build/databackup watch \
  -s /home/user/data \
  -d /mnt/backup/watch.dbak \
  --pack --compress
```

`watch` 会监听源目录变化。检测到创建、修改、删除、移动、属性变化等事件后，会触发一次目录备份。当前实现是“变化后重新备份目录”，不是块级增量备份。

如果 `-d` 指向一个已经存在的目录，程序会在该目录下生成默认归档文件 `databackup-watch.dbak`；如果 `-d` 指向文件路径，则直接使用该文件作为归档输出。

后台 Daemon 模式：

```bash
./build/databackup watch \
  -s /home/user/data \
  -d /mnt/backup/watch.dbak \
  --pack --compress --daemon
```

Daemon 模式下，PID 和日志默认写到归档文件所在目录：

```bash
cat /mnt/backup/databackup-watch.pid
tail -f /mnt/backup/databackup-watch.log
```

停止后台监控：

```bash
kill "$(cat /mnt/backup/databackup-watch.pid)"
```

更完整的课堂演示流程见 [docs/demo.md](docs/demo.md)。

## 命令格式

```text
./build/databackup backup  -s <source>  -d <dest>   [options]
./build/databackup restore -s <archive> -d <target> [options]
./build/databackup watch   -s <source>  -d <dest>   [options]
```

备份和监控常用选项：

```text
--compress
--pack
--encrypt
--password <pwd>
--level <1-9>
--exclude <pattern>
--include-ext <ext>
--min-size <bytes>
--max-size <bytes>
--modified-after <ts>
--modified-before <ts>
--owner <uid>
--exclude-hidden
--exclude-dirs
```

仅 `watch` 模式使用：

```text
--daemon
```

恢复加密归档时使用：

```text
--password <pwd>
```

## 项目结构

```text
DataBackup/
├── CMakeLists.txt
├── include/
│   ├── common/      # 公共类型和平台兼容定义
│   ├── core/        # 核心引擎接口：遍历、过滤、文件读写、备份恢复编排
│   ├── pipeline/    # 数据管道接口：归档格式、打包、压缩、加密
│   └── monitor/     # 监控接口：文件事件、监听器、Monitor 配置
├── src/
│   ├── core/        # 核心引擎实现
│   ├── pipeline/    # 数据管道实现
│   ├── monitor/     # inotify 监控和 daemon 实现
│   └── main.cpp     # CLI 入口
├── tests/           # Google Test / CTest 测试
└── docs/            # 项目文档
```

## 模块说明

### CLI 入口

`src/main.cpp` 负责解析 `backup`、`restore`、`watch` 三个子命令，把命令行参数转换为 `BackupConfig` 和 `FilterOptions`，再调用核心引擎。

### 核心引擎

`include/core/` 和 `src/core/` 负责文件系统相关逻辑：

- `BackupEngine`：编排备份、恢复、实时备份回调。
- `DirectoryTraverser`：递归扫描目录，生成 `FileMetaData` 列表。
- `FileFilter`：执行自定义备份筛选。
- `FileSystemReader`：读取文件内容和元数据。
- `FileSystemWriter`：恢复文件内容和元数据。

### 数据管道

`include/pipeline/` 和 `src/pipeline/` 负责 `.dbak` 文件格式和数据处理：

- `ArchiveWriterImpl` / `ArchiveReaderImpl`：写入和读取归档。
- `ArchiveFormat`：定义归档头、文件条目、Footer 等格式结构。
- `StreamPacker`：多文件打包和解包。
- `StreamCompressor`：RLE 压缩和解压。
- `StreamEncryptor`：RC4 流加密和解密。
- `KeyDerivation`：由密码派生加密密钥。

### 监控模块

`include/monitor/` 和 `src/monitor/` 负责实时备份：

- `Monitor`：基于 Linux inotify 监听目录变化。
- `FileEvent`：描述创建、修改、删除、移动、属性变化等事件。
- `IFileEventListener`：事件回调接口。
- Daemon 支持：后台运行、PID 文件、日志重定向。

## 文档

- [docs/demo.md](docs/demo.md)：课堂演示流程。
- [docs/code-walkthrough.md](docs/code-walkthrough.md)：面向新人的项目代码导读。
- [docs/testing.md](docs/testing.md)：测试框架、测试范围和运行方式。
- [docs/architecture.md](docs/architecture.md)：系统架构说明。
- [docs/core-engine.md](docs/core-engine.md)：核心引擎模块说明。
- [docs/pipeline.md](docs/pipeline.md)：数据管道和归档格式说明。
- [docs/backend-algorithms.md](docs/backend-algorithms.md)：RLE、RC4、KDF 等算法说明。
- [docs/monitor.md](docs/monitor.md)：监控模块说明。
- [docs/encrypt.md](docs/encrypt.md)：加密功能用例说明。

## 开发协作

### 分支命名

```text
feat/<简短描述>      新功能
fix/<简短描述>       修复 bug
docs/<简短描述>      文档修改
refactor/<简短描述>  代码重构
test/<简短描述>      测试修改
chore/<简短描述>     杂项修改
```

### 提交信息

项目采用 Conventional Commits：

```bash
git commit -m "<类型>(<影响范围>): <简短描述>"
```

示例：

```text
feat(core): add owner and modified-time filters
fix(monitor): handle missing watch directory error
docs(readme): update usage guide
test(pipeline): add encrypted archive restore tests
```

### 日常开发流程

```bash
git switch main
git pull origin main
git switch <your-branch>
git rebase main
```

如果 rebase 产生冲突，解决后继续：

```bash
git add .
git rebase --continue
```

推送分支：

```bash
git push
```
