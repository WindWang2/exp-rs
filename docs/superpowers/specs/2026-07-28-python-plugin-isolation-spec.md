# Python 插件系统线程与内存隔离机制设计规范 (Python Plugin System Isolation Spec)

## Problem Statement

当前 `exp-rs` 中的 Python 插件机制直接嵌入在 C++ 主进程的 CPython 解释器中 (`QgisPython`)。此架构存在以下突出痛点：
1. **稳定性风险 (Crash Vulnerability)**：第三方 Python 插件发生的 C/C++ 动态链接库段错误 (Segfault)、内存溢出 (OOM) 或未捕获异常，会直接导致整个遥感 GIS 主程序崩溃。
2. **GIL 锁竞争 (GIL Contention)**：在主进程中执行多线程 Python 任务时受限于全局解释器锁 (GIL)，不仅无法发挥多核 CPU 性能，还会因主 GUI 线程与工作线程争夺 GIL 而导致 UI 卡顿冻结。
3. **环境污染与依赖冲突 (Dependency Conflicts)**：各 Python 插件共享同一个 `sys.path` 和全局变量空间，不同插件所依赖的第三方 Python 库版本冲突容易引起相互干扰。

## Solution

通过 **Out-of-Process Worker Daemon + POSIX 共享内存 + 声明式 RPC UI 代理** 架构，实现 Python 插件系统的彻底隔离：
1. **进程外沙箱 (Out-of-Process Workers)**：每个 Python 插件运行在独立的后台 Daemon 子进程中，主程序崩溃防护率达到 100%。
2. **零拷贝数据交换 (Zero-Copy Shared Memory)**：对于 GB 级遥感大图与矢量矩阵，通过 POSIX 共享内存 (`QSharedMemory` / `shm_open`) 共享缓冲区首地址与元数据，实现 C++ 与 Python 间微秒级 0 拷贝数据传递。
3. **预热进程池与自动愈合 (Pre-warmed Pool & Auto-Healing)**：主程序启动时预热工作进程池，Worker 崩溃时自动隔离异常并秒级重启，保持 0 毫秒加载延迟。
4. **透明接口门面 (`SicnuAppInterface` RPC Proxy)**：代理 Python 端 `iface.addPluginMenu()` 等 UI 操作，对上层插件保持 100% API 兼容。

## User Stories

1. As a remote sensing specialist, I want Python plugins to run in separate background processes, so that a buggy third-party plugin crash (Segfault/OOM) will never crash my main application or lose unsaved project data.
2. As a plugin developer, I want to operate on multi-gigabyte raster arrays directly using Numpy or PyTorch without data copying overhead, so that my algorithm runs at maximum speed over shared memory.
3. As a user, I want the Python plugin execution to be non-blocking, so that the main GIS UI remains completely smooth and responsive during intensive spectral analysis.
4. As a system administrator, I want each Python plugin to run in its own isolated Python environment/sys.path, so that incompatible module dependencies between plugins do not cause conflicts.
5. As a developer, I want Python plugins to use standard `iface.addPluginMenu()` API, so that existing QGIS-style Python plugins can be imported without rewriting UI code.
6. As a QA engineer, I want the system to automatically capture crashed Python plugin stack traces and restart the worker pool silently, so that the application maintains high availability.

## Implementation Decisions

### 1. 架构模块划分与目录结构 (`src/python/isolated/`)
在 `src/python/isolated/` 下新增以下核心模块：
- `python_worker_process.h/.cpp`：Worker 子进程拉起、PID 监控、`SIGCHLD` 信号监听与自动重启护航。
- `python_ipc_server.h/.cpp`：基于 Unix Domain Socket / Named Pipe 的 `QLocalServer` / `QLocalSocket` JSON-RPC 2.0 服务端。
- `shared_memory_segment.h/.cpp`：带 Header 头的 RAII 共享内存管理器，提供 UUID、数据维度、数据类型映射及自动 `shm_unlink` 安全销毁。
- `python_app_interface_proxy.h/.cpp`：C++ 侧 `SicnuAppInterface` 门面代理，将原生 `QAction` 点击事件转换为 RPC 回调 ID 发送给 Python 端。
- `scripts/worker_daemon.py`：Python 端 `asyncio` 事件循环守护进程，动态 `importlib` 加载目标插件模块。

### 2. 交互接口与协议设计 (IPC & Shared Memory API)
- **控制通道 (JSON-RPC 2.0)**：
  - 触发插件初始化：`{"method": "plugin.initialize", "params": {"plugin_dir": "...", "package": "..."}}`
  - UI 注册命令：`{"method": "ui.add_menu", "params": {"title": "计算NDVI", "callback_id": "cb_001"}}`
  - 事件反向回调：`{"method": "ui.on_action_triggered", "params": {"callback_id": "cb_001"}}`
- **数据通道 (Shared Memory Buffer)**：
  - Header 定义：包含 `UUID` (16 bytes), `width` (int32), `height` (int32), `bands` (int32), `dtype` (int32), `ref_count` (atomic int32)。
  - Python 端通过 `numpy.frombuffer(shm.buf, dtype=np.float32)` 无缝挂载。

### 3. 透明替代策略 (`PythonPluginAdapter`)
- 重构 `src/app/python/python_plugin_adapter.cpp`，保持 `SicnuPluginInterface` 契约不变，内部通过 `PythonWorkerProcessPool` 分发任务，实现对外部 `PluginManager` 的无感切换。

## Testing Decisions

### 1. 测试原则 (Test Seams & Behavioral Focus)
- 仅测试最高层外部行为（IPC 消息往返、共享内存数据一致性、崩溃隔离与重启），不测试内部私有实现细节。
- **主测试缝隙 (Primary Seam)**：`PythonPluginAdapter` (`SicnuPluginInterface`) 与 `PythonIpcServer`。

### 2. 单元与集成测试套件 (`tests/test_python_plugin_manager.cpp`)
- **RPC 消息往返验证**：拉起真实 `worker_daemon.py` 子进程，测试初始化与菜单注册 RPC 命令收发。
- **100MB 栅格矩阵零拷贝一致性测试**：C++ 写入 100MB 随机浮点矩阵至共享内存，Python 插件读取并乘以 2.0 写回，C++ 验证校验和一致性。
- **崩溃隔离与自愈测试**：Python 插件故意调用 `os._exit(1)` 或触发 C 扩展段错误，验证 C++ 主进程不被破坏，且 `PythonWorkerProcessPool` 成功捕捉崩溃事件并恢复进程池。

### 3. 现有代码借鉴 (Prior Art)
- 参考 `tests/test_python_plugin_manager.cpp` 中现有的 `SicnuAppInterface` 与 `QgisPython` 测试结构。

## Out of Scope

- 跨机器分布式网络 Python 节点执行（仅限定在单机进程外隔离）。
- 自定义 CPython C-API 硬件 GPU 显存共享（仅针对 POSIX 系统内存共享）。

## Further Notes

- 该规范为后续 Ticket 拆解（Wave 01 ~ Wave 04）提供唯一架构依据。
