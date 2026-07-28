# 01 — Wave A: JSON-RPC 2.0 双向 IPC 通信框架与 Worker 子进程守护

**What to build:**
实现 C++ 主程序与 Out-of-Process Python Worker 守护进程的启动与 IPC 连接。C++ 侧通过 `PythonWorkerProcess` 启动 `worker_daemon.py` 子进程，利用 `QLocalServer`/`QLocalSocket` 建立 Unix Domain Socket 双向管道，并通过 JSON-RPC 2.0 协议完成握手与心跳。

**Blocked by:** None — can start immediately

**Status:** ready-for-agent

## Acceptance Criteria
- [ ] 在 `src/python/isolated/` 中实现 `PythonWorkerProcess` 类，支持拉起与销毁 Python 进程。
- [ ] 实现 `PythonIpcServer` 类，处理基于 Socket 的 JSON-RPC 2.0 异步消息路由。
- [ ] 创建 `src/python/scripts/worker_daemon.py`，实现 `asyncio` Socket 客户端监听与方法分发。
- [ ] 在 `tests/test_python_plugin_manager.cpp` 中编写 Catch2 测试，验证进程启动与 Ping/Pong 握手成功。
