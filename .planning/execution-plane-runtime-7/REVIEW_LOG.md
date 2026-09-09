# REVIEW_LOG — 自审 + 对抗性评审记录

## 自审发现与修复（实现过程中）

| # | 发现 | 严重度 | 处置 |
|---|---|---|---|
| 1 | worker 主循环 cancelFlag 从不复位：复用 worker 时上一个被取消的 job 会让下一个 job 立即自我取消（池靠"取消即退役"掩盖，M2 引入复用后爆发） | P1 | 修复：每次 run 前复位 cancelFlag（sicnu_worker_main.cpp） |
| 2 | LocalWorkerPool 整池单 owner-thread 纪律与 JobEngine 多线程 dispatch 不兼容（跨线程 run 直接 throw） | P0（接线阻断） | 重设计为每-worker ownerThread；acquire 只返回本线程 spawn 的 worker；外来孤儿槽位强制回收自愈 |
| 3 | Require 模式 fail-closed 缺口：shouldRunIsolated 读 pool-started 状态，池不可用时静默变 Off → 违反 fail-closed | P1 | 重设计 configured-mode（g_configuredMode 原子；ensure/setWorkerExecutionMode 显式置位） |
| 4 | markTaskFailed 重试路径早退不 flush staged launch → 任务滞留 Dispatching | P0 | 重构：autoRetried 标志 + 统一锁外 flushPendingLaunches/Signals，跳过 cascade/终态回调 |
| 5 | 池析构竞态：drain 检查后进入的 run() 与成员析构并发（UAF 窗口） | P1 | m_destroying 拒绝关闭新 run |
| 6 | worker result payload 注入 __workerDiagnostics 会污染缓存 payload 与下游消费者 | P2 | 改走 ctx.logWarning 日志通道 |
| 7 | identify 笔误 `not drivableByThisThread`（非法标识符） | 编译期 | 更名 foreignIdle |
| 8 | waitForTask/waitForPipeline "持锁等待"审计疑点 → 复核为误报：QWaitCondition::wait 等待期释放 m_mutex，唤醒间竞争极小 | 记录 | 不改动；结论记录在案 |
| 9 | image_fusion GUI/CLI 直连路径绕过 OutputCommitter（#617 残留） | P2（已知限制） | 已有 OutputCleanupGuard 防截断产物；完整 committer 化涉及 GUI 重构，记录到 FINAL_REPORT 已知限制 |
| 10 | 磁盘 GC 配额"缺失"（审计条目）→ 复核为审计过时：SICNU_ARTIFACT_CACHE_MAX_GB（默认 8GB）已在 execution_fingerprint.cpp:645 生效 | 记录 | 避免重复开发；测试保持既有覆盖 |

## Subagent #2 — 最终对抗性评审
（待 M0 构建验证 + 自测通过后执行）
