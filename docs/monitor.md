# 系统监控模块

> `include/monitor/` | inotify 文件系统事件监听、Daemon 守护进程、增量备份

## 状态

模块接口已定义，实现待完成。

## 组件

| 类/文件 | 说明 |
|---|---|
| `FileEvent` | 文件系统事件结构（路径、事件类型、时间戳） |
| `IFileEventListener` | 事件回调接口（观察者模式） |
| `IMonitor` | 监控器接口，支持 Daemon 化 |

## FileEvent

```cpp
struct FileEvent {
    std::string path;       // 触发事件的文件路径
    enum Type { CREATED, MODIFIED, DELETED, MOVED };
    Type type;
    std::chrono::system_clock::time_point timestamp;
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
    virtual bool start(const std::string& watchPath) = 0;
    virtual void stop() = 0;
    virtual void addListener(IFileEventListener* listener) = 0;
    virtual bool daemonize() = 0;  // POSIX Daemon 化
};
```

## 技术要点

| 技术 | 说明 |
|---|---|
| Linux inotify API | `inotify_init`, `inotify_add_watch`, 事件轮询 |
| POSIX Daemon 化 | `fork()` → `setsid()` → 重定向标准 I/O |
| 防抖机制 | 合并短时间内的重复事件，避免频繁触发备份 |
| 增量备份触发 | 文件变更时自动调用 `BackupEngine::onFileEvent()` |

## 计划实现

1. `InotifyMonitor` — 基于 inotify 的监控器实现
2. `DaemonManager` — 守护进程生命周期管理
3. `IncrementalBackupTrigger` — 增量备份触发逻辑
4. CLI `watch` 子命令 — `databackup watch -s /path --daemon`

## 相关文档

- [系统架构](architecture.md)
- [核心引擎模块](core-engine.md)
