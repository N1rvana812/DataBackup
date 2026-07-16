# 软件演示流程

> 面向课程展示：按顺序演示编译、测试、备份、恢复、压缩、打包、加密和实时监控。

## 1. 编译项目

在项目根目录执行：

```bash
cmake -S . -B build -DBUILD_TESTS=ON
cmake --build build -j$(nproc)
```

说明点：

- `build/databackup` 是主程序。
- `build/tests/databackup_tests` 是 gtest 测试程序。
- `databackup_lib` 是核心静态库，CLI 和测试都复用它。

## 2. 运行测试

```bash
ctest --test-dir build --output-on-failure
```

预期结果：

```text
100% tests passed
```

说明点：

- 测试覆盖核心引擎、文件系统读写、归档格式、数据管道、备份恢复编排和 Monitor。
- 当前 CTest 中包含 gtest 用例、CLI help 冒烟测试和 Monitor 集成测试。

## 3. 准备演示目录

```bash
rm -rf /tmp/dbdemo
mkdir -p /tmp/dbdemo/source/docs
echo "hello databackup" > /tmp/dbdemo/source/hello.txt
echo "readme content" > /tmp/dbdemo/source/docs/readme.md
printf 'AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n' > /tmp/dbdemo/source/repeated.txt
```

查看源目录：

```bash
find /tmp/dbdemo/source -type f -print -exec cat {} \;
```

## 4. 基础备份

```bash
./build/databackup backup \
  -s /tmp/dbdemo/source \
  -d /tmp/dbdemo/basic.dbak
```

说明点：

- `backup` 表示创建备份。
- `-s` 是源目录。
- `-d` 是输出的 `.dbak` 归档文件。
- 核心引擎会遍历目录，读取文件，并交给归档模块写入。

## 5. 基础恢复

```bash
./build/databackup restore \
  -s /tmp/dbdemo/basic.dbak \
  -d /tmp/dbdemo/basic_restore
```

验证恢复结果：

```bash
diff -r /tmp/dbdemo/source /tmp/dbdemo/basic_restore
```

如果 `diff` 没有输出，说明恢复内容和源目录一致。

## 6. 压缩、打包、加密备份

```bash
./build/databackup backup \
  -s /tmp/dbdemo/source \
  -d /tmp/dbdemo/secure.dbak \
  --compress --pack --encrypt --password "123456"
```

说明点：

- `--compress` 启用 RLE 压缩。
- `--pack` 启用多文件打包。
- `--encrypt` 启用 RC4 流加密。
- `--password` 指定加密密码。

恢复加密备份：

```bash
./build/databackup restore \
  -s /tmp/dbdemo/secure.dbak \
  -d /tmp/dbdemo/secure_restore \
  --password "123456"
```

验证：

```bash
diff -r /tmp/dbdemo/source /tmp/dbdemo/secure_restore
```

## 7. 错误密码演示

```bash
./build/databackup restore \
  -s /tmp/dbdemo/secure.dbak \
  -d /tmp/dbdemo/wrong_password_restore \
  --password "wrong"
```

说明点：

- 加密归档需要正确密码才能恢复。
- 该步骤用于展示加密保护效果。

## 8. 实时监控备份

打开第一个终端，启动监控：

```bash
./build/databackup watch \
  -s /tmp/dbdemo/source \
  -d /tmp/dbdemo/watch.dbak \
  --compress --pack
```

打开第二个终端，修改源目录：

```bash
echo "new file" > /tmp/dbdemo/source/new.txt
echo "changed" >> /tmp/dbdemo/source/hello.txt
```

第一个终端预期出现类似输出：

```text
[INFO] Detected file change, triggering backup...
[INFO] Backup completed: files=..., directories=..., bytes=...
```

恢复监控生成的备份：

```bash
./build/databackup restore \
  -s /tmp/dbdemo/watch.dbak \
  -d /tmp/dbdemo/watch_restore
```

验证：

```bash
diff -r /tmp/dbdemo/source /tmp/dbdemo/watch_restore
```

停止监控时，在第一个终端按 `Ctrl+C`。

## 9. Daemon 模式演示

```bash
./build/databackup watch \
  -s /tmp/dbdemo/source \
  -d /tmp/dbdemo/watch_daemon.dbak \
  --compress --pack --daemon
```

说明点：

- `--daemon` 会让监控进程后台运行。
- PID 文件和日志文件默认写到归档目标所在目录：
  - `/tmp/dbdemo/databackup-watch.pid`
  - `/tmp/dbdemo/databackup-watch.log`

查看后台进程：

```bash
cat /tmp/dbdemo/databackup-watch.pid
ps -p "$(cat /tmp/dbdemo/databackup-watch.pid)"
```

查看日志：

```bash
tail -f /tmp/dbdemo/databackup-watch.log
```

停止 daemon：

```bash
kill "$(cat /tmp/dbdemo/databackup-watch.pid)"
```

## 10. 帮助信息

```bash
./build/databackup --help
```

说明点：

- 当前 CLI 支持 `backup`、`restore`、`watch` 三个子命令。
- 用户可以通过 `--help` 查看参数说明。

## 推荐讲解顺序

1. 核心引擎负责目录遍历、文件读取、文件写入和元数据恢复。
2. 数据管道负责 `.dbak` 格式、压缩、打包和加密。
3. Monitor 负责监听文件变化，并触发自动备份。
4. CLI 把各模块组合为 `backup`、`restore`、`watch` 三个命令。
5. 测试覆盖主要模块，演示前先用 `ctest` 证明当前版本通过自动化测试。
