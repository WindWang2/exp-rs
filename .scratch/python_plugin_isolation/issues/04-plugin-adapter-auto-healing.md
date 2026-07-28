# 04 — Wave D: PythonPluginAdapter 透明替代与崩溃自动自愈

**What to build:**
重构 `PythonPluginAdapter`，全面继承 `SicnuPluginInterface`，无缝替代旧版 `QgisPython` 直调逻辑。同时实现 Worker 进程崩溃监听（`SIGCHLD`），在 Python 插件发生 Segfault / `os._exit(1)` 时隔离崩溃并秒级拉起新 Worker 恢复进程池。

**Blocked by:** 02 — Wave B: RAII 共享内存管理器与 0 拷贝栅格矩阵共享, 03 — Wave C: SicnuAppInterface RPC 代理门面与 UI 声明式桥接

**Status:** ready-for-agent

## Acceptance Criteria
- [ ] 重构 `src/app/python/python_plugin_adapter.cpp`，内部通过 `PythonWorkerProcessPool` 分发任务。
- [ ] 实现崩溃隔离与护航守护：捕捉 Python 进程异常退出，主界面不崩溃并弹出友好的警示信息。
- [ ] 实现 `PythonWorkerProcessPool` 秒级自动拉起替代进程机制。
- [ ] 在 `tests/test_python_plugin_manager.cpp` 中编写集成测试，验证真实 Python 插件加载与崩溃自愈过程。
