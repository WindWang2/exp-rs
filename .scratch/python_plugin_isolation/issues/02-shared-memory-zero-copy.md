# 02 — Wave B: RAII 共享内存管理器与 0 拷贝栅格矩阵共享

**What to build:**
实现基于 POSIX 共享内存 (`shm_open` / `QSharedMemory`) 的零拷贝大图与矢量矩阵交换。C++ 侧通过 RAII 包装类记录 Header 元数据与引用计数，销毁时安全 `shm_unlink`；Python 端通过 `numpy.frombuffer()` 无缝挂载。

**Blocked by:** 01 — Wave A: JSON-RPC 2.0 双向 IPC 通信框架与 Worker 子进程守护

**Status:** ready-for-agent

## Acceptance Criteria
- [ ] 在 `src/python/isolated/` 中实现 `SharedMemorySegment` RAII 共享内存管理类。
- [ ] 定义包含 UUID、宽度、高度、波段数、DataType 及引用计数的 Shared Header。
- [ ] 在 `worker_daemon.py` 中实现共享内存挂载函数，转换为 numpy ndarray。
- [ ] 在 `tests/test_python_plugin_manager.cpp` 中编写 Catch2 测试，验证 100MB 随机浮点矩阵零拷贝收发与校验和一致性。
