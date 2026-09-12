# FINAL REPORT — Execution / Concurrency / Lifecycle Platform 9.0

分支：`feat/execution-concurrency-lifecycle-9`（自 `8f6293bceb` = origin/master `132da5e998`
+ 未推送的 #848-#852 P0 修复提交——随本 PR 进入远端，避免孤儿修复）。
主机证据：Linux 6.18 x64 / 16C / 62GB / GCC 16.2.1 / Qt 6.11 / GDAL 3.13.3 / Release / Ninja。
CI 政策：**未等待线上 CI/CD；完成依据为本地可复现证据**（见 VERIFICATION.md）。

## 交付总览（M0-M8）

| 包 | 交付 | 关键位置 |
|---|---|---|
| M0 #860 | coordinator 状态迁移与通知分离：锁内排队、锁外 drain（重入安全 + seq 序号戳进 trace）；观察者同线程重入合法化 | workflow_run_coordinator.{h,cpp} |
| M0 #876 | resume swap 的 ghost run 闭合：forceSetState(Canceled) + typed error + 持久化 + 终态广播，随后才移除映射/checkpoint | workflow_run_coordinator.cpp resumeRunImpl |
| M0 #862 | TaskCenter 准入层 transient child：worker 线程提交的子任务绕过 globalMax/profile/isolated/RAM/budget2/RSS 软闸（有界 kMaxTransientChildren=8，镜像 JobEngine #798）；waitForTask/waitForPipeline worker 线程 typed 拒绝（返回真实快照 + trace） | task_center.{h,cpp} |
| M1 | 显式 structured hierarchy：JobEngine currentJobId (thread_local) → ownerTaskId → m_ownedChildren；join 规则（owner terminal ⇒ 孤儿子任务结构化取消，invariant I9）；取消传播走 DAG+ownership 双边；与 DAG 数据依赖正交 | job_engine.{h,cpp}、task_center.{h,cpp} |
| M2 | #852 契约化：快照 reader 的过时 off-affinity 警告移除（与 CatalogSnapshot 契约一致）；generation 单调性并发测试；worker 读合法化 | data_manager.cpp、test_execution_plane_9 |
| M3 | TaskCancelReason 类型化取消源（User/Upstream/Shutdown/StructuredJoin）贯通 cancelTask/cascade/dispatch/listener；terminal 单调守卫（I2，seam 级 + Q_ASSERT）；pause/resume typed 行为钉测 | task_center.{h,cpp} |
| M4 | 全 emit 点审计：TaskCenter（本就锁外）、JobEngine（本就锁外）、coordinator（本次修复）；通知 seq 并入 trace detail | REVIEW_LOG 审计矩阵 |
| M5 | admissionSnapshot 增 transientActive/transientCap；terminal 迁移一次 RSS 观测进有界 trace | task_center.{h,cpp} |
| M6 | 复验 8.0 交付（temp+rename 原子写、SICNU_FAULT_POINT、损坏 typed 拒绝、身份门、moved-output 重水化）全部在位；新增 corrupt checkpoint typed refusal 测试 | workflow_checkpoint.cpp、test_execution_plane_9 |
| M7 | TaskCenter::explainDump + WorkflowRunCoordinator::explainRun/explainDump：饱和度、transient 用量、队列深度、per-status 计数、run 步级证据；锁内拷贝锁外格式化 | task_center.cpp、workflow_run_coordinator.cpp |
| M8 | 300 步深链、60-task cancel storm、100k 逻辑规模（SICNU_EP9_STRESS 门控）；既有 ep7 10k 压测回归 | tests/test_execution_plane_9.cpp |

## Issue 关闭映射

- **#851 / #852 / #848 / #849 / #850**：fixed-by `8f6293bceb`（本地 master 未推送，随本 PR 上远端）；
  本方向复验其修复与回归测试在位。
- **#860 / #862 / #876**：fixed-by 本方向 commits（回归测试位于 test_execution_plane_9.cpp，
  旧代码必失败的确定性 reproducer）。
- **#859 / #861 / #858**：workbench/UI 轨所有权；本方向仅提供生命周期契约（锁外通知、
  shutdown drain、typed cancel），不修改 `src/app/**`。

## 核心不变量守护（ARCHITECTURE I1-I9）

| 不变量 | 守护 |
|---|---|
| I2 终态单调 | setTaskStatusLocked seam 守卫 + trace（terminal_monotonicity_violation）|
| I3 锁内无 emit/外部回调 | 三层全部锁外（TaskCenter flushPendingSignals、JobEngine notify、coordinator drain）|
| I4 worker 等待的 child 必可发射 | transient admission（全局 gate 遮挡修正为 mark-and-continue）+ wait typed 拒绝 |
| I5 cancel 最终可观察 | cancel storm 测试 + JobEngine 完成 flag 检查 |
| I6 旧 generation 迟到回调丢弃 | pipeline/runId 映射缺失天然丢弃 + 测试钉住 |
| I7 无永久 ghost | #876 修复 + "sawRunning ⇒ sawTerminal" 测试不变量 |
| I8 调度资源一切有界 | transient bypass ≤8；trace 环有界；通知队列每迁移 1 条；100k 门控断言（任务 map 的完成态保留沿用既有清理策略，非 9.0 新增无界类） |
| I9 join | terminal 钩子 staging + flushOwnedCancels，join 测试 |

## 真实测试证据（全部本地实测，详见 VERIFICATION.md）

| 套件 | 结果 |
|---|---|
| **test_execution_plane_9（新增）** | **865 assertions / 14 cases 全绿** |
| test_task_center / test_job_engine | 382/32、446/34 全绿 |
| test_workflow_run_coordinator / resume_provenance | 172/10、67/2 全绿 |
| test_worker_host / temporal_workspace | 56/12、267/21 全绿 |
| test_execution_plane_7 / 8 / concurrency_stress / fault_registry | 50/9、103/13、2152/6、31/9 全绿 |
| 100k 逻辑规模（SICNU_EP9_STRESS=1） | 6.075s / 100,005 assertions 通过 |
| 合计 | **≈4,700+ assertions 全绿** |

## 对抗评审（2 个独立只读 subagent）

- Reviewer A（架构/并发/正确性）：初判 not-mergeable（1 P1 + 2 P2 + 7 P3）——
  P1（explainDump 锁外调用 Locked 辅助）、P2×2、可行动 P3 全部修复。
- Reviewer B（测试/性能/可移植性/文档一致性）：初判 mergeable（1 P1）——
  P1（#862 reproducer 确定性）、P2×2、P3×5 全部修复。
- 全部处置与接受理由见 REVIEW_LOG.md；修复后受影响套件重跑全绿。

## 已知限制与后续

1. transient child 对 RSS watermark 的绕过是有意的 liveness 取舍（有界 8；ARCHITECTURE/REVIEW_LOG 记录）。
2. coordinator 通知 seq 目前仅进 trace（runStateChanged 签名保持兼容）；如需观察者侧重排序，
   后续可加 V2 signal（未做——避免投机 API）。
3. Windows Job Object / OTB / ONNX Runtime lane 本机不可运行，如实声明。
4. 其他并行 `-9` track 共享 tests/CMakeLists.txt（本方向仅在自身区块追加）。
