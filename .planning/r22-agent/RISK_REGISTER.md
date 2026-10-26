# R22-04 (r22-agent) CROSS_TRACK_FINDINGS / RISK_REGISTER

## 跨轨发现（本轨只读或不触碰，交由归属轨处理）

1. **`src/python/scripts/worker_daemon.py`（#1692 daemon 侧）**：任务书白名单列 `src/python/isolated/**`；worker_daemon.py 位于 `src/python/scripts/`，是 isolated IPC 协议的 daemon 对端，R22-04 D1 明确要求"daemon/host 协议互证"。本轨对该文件的最小修改（>1MiB 响应回 typed error -32001）已在 OWNERSHIP.md 备案；无任何 open PR 触碰该文件（#1673/#1674/#1675 文件清单核实）。若评审认为越界，可单独 revert 该 hunk——宿主侧修复已独立成立。
2. **#1693-1（StdinReader 死代码）**：位于 `src/agent/mcp_server.cpp` StdinReader::run（~411-467），与 #1675（R21-C）在该文件的改动（dispatch 区 ~939-1190）不同 hunk；merge 顺序无语义依赖。
3. **tests/CMakeLists.txt**：本轨 4 个 append-only 注册块（16446 起），与 #1673/#1674 的追加块物理隔离；兄弟 R22 轨（data/science/inference/workbench）后续 push 时需按块重排。

## RISK_REGISTER（实施中新增）

| 风险 | 状态 |
|---|---|
| 共享构建锁排队（r22-workbench 单目标构建 >37min） | 已等待；configure 完成前无法 RED/GREEN，不视为产品阻塞 |
| ghost-algo 测试需真实 worker daemon + python3 | 同 test_python_plugin_host 运行条件；若环境不可用标 NOT_EXECUTED 并给代码级证据 |
| JobEngine 常量为 private → 测试用文档化字面量断言 | 接受（不改可见性避免 API 面） |
| #1704 `data:a.tif`（POSIX 冒号文件名）行为保持 LocalPath | 已由 unknownDriverPayloadIsDangerous 的 benign 分支 + 专项测试守护 |
