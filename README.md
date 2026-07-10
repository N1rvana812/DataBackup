# DataBackup

> 基于 Linux 终端的高效、安全、自动化数据备份工具


## 项目开发须知

>请大家按照以下规范开始建立自己的开发分支

### 一、首次获取项目

#### 拉取最新代码，进入工作目录并切换到develop

```bash
git clone https://github.com/N1rvana812/DataBackup.git
cd DataBackup
git switch develop
```

### 二、创建所需功能的开发分支
例如
```bash
git switch -c feat/daemon
git push -u origin feat/daemon
```
以后即可直接：
```bash
git push
```
#### 分支名：<类型前缀>/<简短描述>
- feat/ 开发功能
- fix/ 修复bug
- docs/ 修改文档
- refactor/ 代码重构
- test/ 添加或修改测试代码
- chore/ 杂项修改


### 三、开始开发

#### 查看当前分支，确保是功能分支
```bash
git branch
#输出如 * feat/datapush
```
### 四、提交代码
#### 添加所有修改并提交
#### 采用Conventional commits
```bash
git commit -m "<类型>(<影响范围>): <简短描述>"
```
例如：
- feat(ui): 添加备份进度条组件
- fix(core): 修复大文件备份时内存溢出的问题
- docs(readme): 更新项目编译说明
- refactor(compress): 重构ZIP压缩逻辑，降低耦合度
### 五、上传到远程仓库
#### 第一次上传
```bash
git push -u origin feat/<功能>
```
#### 后续可以直接push

### 六、每日开发

1. 确保本地 main 是最新的
```bash
git switch main
git pull origin main
```
2. 切回自己的功能分支
```bash
git switch feature-compress
```

3. 把自己的分支 rebase 到最新的 main 上
```bash
git rebase main
```
如果提示冲突，打开代码编辑器解决冲突，然后 
```bash
git add . && git rebase --continue
```

4. 推送到远程 (因为 rebase 重写了历史，可能需要强制推送)
```bash
git push origin feature-compress -f 
```

5. 去 GitHub 上发起 Pull Request


## 快速开始（目标软件的运行方式）

```bash
# 编译
mkdir build && cd build
cmake ..
make

# 运行
./databackup --help

# 基础备份
./databackup backup -s /home/user/data -d /mnt/backup/data.dbak

# 带压缩和加密的备份
./databackup backup -s /home/user/data -d /mnt/backup/data.dbak \
    --compress --encrypt --password "MySecretKey"

# 实时备份（Daemon 模式）
./databackup watch -s /home/user/data -d /mnt/backup/ --daemon
```

## 项目结构说明

```bash
DataBackup/
├── CMakeLists.txt # CMake 构建配置文件
├── docs/ # 项目文档
├── include/ # 头文件（接口定义）
├── src/ # 源代码（接口实现）
├── tests/ # 单元测试
└── third_party/ # 第三方依赖库
```

### 核心模块分工

#### include/ 和 src/core/ - 核心引擎模块（负责人：吕涛）
- **功能**：文件系统遍历、自定义规则过滤、基础读写、元数据提取与恢复
- **关键接口**：
  - `IFileReader.h`：流式文件读取接口
  - `IFileWriter.h`：文件写入与元数据恢复接口
- **技术要点**：C++17 `<filesystem>`、POSIX `stat/chmod/chown`

#### include/ 和 src/pipeline/ - 数据管道模块（负责人：吕书武）
- **功能**：自定义格式打包/解包、流式压缩/解压、对称加密/解密
- **关键接口**：
  - `IArchiveWriter.h`：归档写入接口（支持压缩+加密管道）
  - `IArchiveReader.h`：归档读取接口（支持解密+解压管道）
- **技术要点**：zlib deflate/inflate、OpenSSL AES-CTR、流式处理（4KB Chunk）

#### include/ 和 src/monitor/ - 系统监控模块（负责人：倪申超）
- **功能**：基于 inotify 的文件系统事件监听、Daemon 守护进程、增量备份触发
- **关键接口**：
  - `IMonitor.h`：监控器接口（支持 Daemon 化）
  - `IFileEventListener.h`：事件回调接口（观察者模式）
  - `FileEvent.h`：文件系统事件结构
- **技术要点**：Linux inotify API、POSIX Daemon 化（fork/setsid）、防抖机制

#### tests/ - 测试模块（负责人：倪申超）
- **功能**：gtest 单元测试、Mock 对象、Valgrind 内存检测
- **技术要点**：Google Test 框架、接口 Mock 测试

#### docs/ - 文档目录
- 需求分析说明书.docx
- 系统设计文档.docx
- 软件测试报告.docx
- UML 用例图、类图、时序图

#### third_party/ - 第三方依赖
- CLI11：命令行参数解析库
- nlohmann/json：JSON 配置文件解析
