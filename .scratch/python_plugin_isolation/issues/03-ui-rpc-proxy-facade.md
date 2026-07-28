# 03 — Wave C: SicnuAppInterface RPC 代理门面与 UI 声明式桥接

**What to build:**
实现 Python 端与 C++ 侧的声明式 UI 代理。Python 插件继续使用标准的 `iface.addPluginMenu()` 接口注册动作，底层将其转换为 RPC 消息传给 C++；C++ 主程序生成原生 `QAction`，并在用户点击时通过 IPC 触发 Python 端的回调函数。

**Blocked by:** 01 — Wave A: JSON-RPC 2.0 双向 IPC 通信框架与 Worker 子进程守护

**Status:** ready-for-agent

## Acceptance Criteria
- [ ] 在 Python 端实现 `SicnuAppInterface` RPC 代理门面类。
- [ ] 在 `src/python/isolated/` 中实现 `PythonAppInterfaceProxy`，处理 C++ 侧动作创建与事件派发。
- [ ] 实现菜单项 `addPluginMenu()` 注册与点击回调 ID 绑定。
- [ ] 在 `tests/test_python_plugin_manager.cpp` 中编写 Catch2 测试，验证 UI 动作注册与点击回调双向通信。
