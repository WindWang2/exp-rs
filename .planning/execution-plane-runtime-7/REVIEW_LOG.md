# REVIEW_LOG — 自审 + 对抗性评审记录

## 自审发现与修复（实现过程中）

| # | 发现 | 严重度 | 处置 |
|---|---|---|---|
| 1 | worker 主循环 cancelFlag 从不复位：复用 worker 时上一个被取消的 job 会让下一个 job 立即自我取消 | P1 | 修复：每次 run 前复位 cancelFlag |
| 2 | LocalWorkerPool 整池单 owner 纪律与 JobEngine 多线程 dispatch 不兼容 | P0（接线阻断） | 重设计为每-worker ownerThread + 槽位自愈 |
| 3 | Require 模式 fail-closed 缺口：mode 读 pool-started 状态而非 configured 状态 | P1 | configured-mode 原子语义（env 读一次 + 显式置位） |
| 4 | markTaskFailed 重试路径早退不 flush staged launch → 任务滞留 Dispatching | P0 | 重构：autoRetried 标志 + 统一锁外 flush |
| 5 | 池析构竞态：drain 后进入的 run() 与成员析构并发 | P1 | m_destroying 拒绝关闭新 run |
| 6 | 诊断注入 result payload 会污染缓存与下游消费者 | P2 | 改走 ctx.logWarning 日志通道 |
| 7 | waitForTask"持锁等待"疑点 → 复核为误报（QWaitCondition 等待期释放锁） | 记录 | 不改动 |
| 8 | 磁盘 GC 配额"缺失"（审计条目过时）→ SICNU_ARTIFACT_CACHE_MAX_GB 已生效 | 记录 | 避免重复开发 |
| 9 | image_fusion GUI/CLI 直连路径绕过 OutputCommitter（#617 残留） | P2（已知限制） | 既有 OutputCleanupGuard 防截断；完整 committer 化记录为后续 |
| 10 | 路由模式在池重启时被 env 覆盖 | P1 | flush 用 currentWorkerExecutionMode() 传播 |
| 11 | 重试路由条件 !hasJobRequest 导致重试静默回退进程内 | P0 | 谓词改为 !jobExecutor（fail-closed 保持） |
| 12 | OpenCV 并行后端 INFO 日志写 stdout，污染 worker 帧流（真实算子首次运行即"malformed frame"） | P0（Windows 首次暴露） | worker 启动时 OpenCV 日志降噪；host 侧 malformed frame 类型化上报（携带原始行，不再误报 timeout） |
| 13 | worker 单线程设计使 cancel 帧在作业执行期间不可达——合作式取消从未真正生效（靠立即击杀掩盖） | P1（设计缺陷） | 作业移至工作线程；主循环保持读帧；ack 先于 arming |

## Subagent #2 — 最终对抗性评审（2026-09-10）

**VERDICT: 0 × P0, 2 × P1, 10 × P2**（评审范围 master...HEAD 全部 6 提交）

评审确认 OK 的高风险面：retry resurrect 恰好一次语义、staging/flush 拆分、
listener 重入、isolated-slot 记账、协议 v1 兼容（逐字节核对新旧 host/worker
组合）、executor 生命周期、锁序（engine listener 无锁调用 ⇒ 重试路径
submitWithId 无自死锁）。

### P1 与修复

| # | 发现 | 修复 |
|---|---|---|
| R1 | 池 m_mutex 持锁跨越 spawn+30s 握手+内联拆除（最坏 ~40s 护卫队，阻塞所有 engine worker/shutdown） | **已修**：拆分 takeIdleWorkerLocked（纯记账）+ spawnWorker（锁外 spawn+握手）+ teardownWorker（锁外有界拆除 ~2s/worker）；run() 改为"锁内预留槽位 → 锁外 spawn → 失败归还槽位"；shutdown 路径对 idle worker 缩短为 1s+1s（idle 无在飞作业，硬杀无损） |
| R2 | 拆除路径跨线程驱动 QProcess（foreign idle / shutdown / 析构）：互斥成立但超出 Qt 文档契约 | **部分修**：拆除限定为 idle worker（互斥保证成立）+ 有界等待 + 契约注释（direct-use 池不得 start()/run() 并发；shared 池从不重启）。完整 owner-thread marshal 记为后续工作 |

### P2 与处置

| # | 发现 | 处置 |
|---|---|---|
| R3 | transient 前缀可被算子伪造；重试丢 caller cancel hook | hook 改 move→copy（已修）；伪造面记录（有界 + 仅扩大自身重试预算） |
| R4 | "cannot send"/"malformed frame" 未归入 transient | 已修：加入分类器（均无副作用，可安全重试） |
| R5 | identity resolver 无消费者 | 头文件改标 SEAM ONLY（接线为后续），不谎称已接线 |
| R6 | ~TaskCenter → shutdownSharedWorkerPool 与 g_pool 静态析构顺序 | 已修：池故意泄漏（进程生命周期单例），shutdown 显式 |
| R7 | 新 worker 对含 "cancelled" 子串的消息打 code；master host 只认精确文本 | 核实为 master 原有行为（消息文本未变，旧 host 行为不变）；worker 注释固化 |
| R8 | runHangJob 发帧未持 stdoutMutex | 已修 |
| R9 | 复活路径缺 updatePipelineForTaskLocked | 已修 |
| R10 | mode TOCTOU 窗口（仅测试 API 可达） | 记录（注释），不阻塞 |
| R11 | g_isolatedJobsRunning 死变量；flush fail-closed 分支实际不可达（真实不可用走 per-job "cannot start"）；kProtocolVersion 未用 | 已清理/注释 |
| R12 | 阻塞式 agent plan 的 DataManager 线程亲缘限制 | 头注释固化（fail-closed 可见）；async 路径亲和正确 |

## 修复后验证（Windows 主机, Debug, 本地证据）

- test_worker_host：**12/12（56 断言）** — 含 caps 协商、acked cancel、
  legacy v1 兼容、真实算子 worker 执行（首次在 Windows 全绿）
- test_execution_plane_7：**9/9** — 故障矩阵逐用例独立进程
  （run_all_ep7.cmd；与 ctest catch_discover_tests 的拆分方式一致）
- 回归护栏 8/11 通过；3 个失败均为既有 Windows 平台限制（非本分支回归）：
  - test_output_committer：POSIX 只读目录语义（QFile 权限在 Windows 不生效）
  - test_workflow_run_coordinator：QFile::setFileTime 需打开句柄（POSIX 语义）
  - test_workflow_recovery：QLockFile owner 行格式与 flock 分支不同
- test_post_process：11/11（对照：recode 进程内路径）
