# 系统监控模块

> `include/monitor/` | inotify 文件系统事件监听、Daemon 守护进程、增量备份

## 状态

模块已实现：`Monitor` 基于 Linux inotify 递归监听目录变更，支持后台线程、重复事件防抖、PID 文件、日志重定向和 `watch` CLI 子命令。

## 组件

| 类/文件 | 说明 |
|---|---|
| `FileEvent` | 文件系统事件结构（路径、事件类型、时间戳） |
| `IFileEventListener` | 事件回调接口（观察者模式） |
| `IMonitor` | 监控器接口，支持 Daemon 化 |
| `Monitor` | inotify 监控器实现 |

## FileEvent

```cpp
struct FileEvent {
    std::string filePath;
    FileEventType type;
    bool isDirectory;
    uint64_t timestamp;
};
```

## IFileEventListener

观察者模式回调接口：

```cpp
class IFileEventListener {
public:
    virtual void onFileEvent(const FileEvent& event) = 0;
};
```

## IMonitor

监控器接口：

```cpp
class IMonitor {
public:
    virtual bool start(const MonitorConfig& config,
                       std::shared_ptr<IFileEventListener> listener) = 0;
    virtual void stop() = 0;
    virtual bool isRunning() const = 0;
};
```

## 技术要点

| 技术 | 说明 |
|---|---|
| Linux inotify API | `inotify_init1(IN_NONBLOCK)`, `inotify_add_watch`, 非阻塞事件轮询 |
| POSIX Daemon 化 | `fork()` → `setsid()` → 重定向标准 I/O |
| 防抖机制 | 合并短时间内的重复事件，避免频繁触发备份 |
| 增量备份触发 | 文件变更时自动调用 `BackupEngine::onFileEvent()` |

## CLI watch 模式

```bash
./databackup watch -s /path/to/source -d /path/to/archive.dbak \
    --compress --pack --encrypt --password secret

./databackup watch -s /path/to/source -d /path/to/archive.dbak --daemon
```

`--daemon` 模式下，PID 文件和日志文件默认写到归档目标所在目录：

- `databackup-watch.pid`
- `databackup-watch.log`

## 相关文档

- [系统架构](architecture.md)
- [核心引擎模块](core-engine.md)
