# ISSUE TRIAGE — 最新基点 `8f6293bceb` 上逐条复验（2026-09-11）

复验方法：在基点源码上重新定位（不按 issue 行号机械对照），逐条分类
`still-valid / fixed-by-later-merge / changed-root-cause / duplicate / cannot-reproduce / out-of-scope`。

## 本方向 ownership（execution/runtime）核心 issues

### #851 [critical] TaskCenter::flushPendingLaunches self-deadlock — **fixed-by-later-merge（本地 8f6293bceb，未推送）**
- 复验：`task_center.cpp` flushPendingLaunches 中 `JobEngine::cancel` 已移出 `m_mutex`
  （`jobToCancel` 延迟路径，含注释引用本 issue）。附带回归测试于 `tests/test_task_center.cpp`。
- 本方向动作：M0 验证回归测试真实性（旧代码必失败）；PR 描述中声明该修复随本 PR 上远端；
  关闭映射：fixed-by <commit>。

### #852 [critical] DataManager temporal workspace 跨线程撕裂读 — **fixed-by-later-merge（本地），M2 契约化**
- 复验：8f6293bceb 对 `data_manager.{h,cpp}` 增加快照提取（+145 行）并有
  `tests/test_temporal_workspace.cpp` 新用例。
- 本方向动作：M2 将"主线程权威 + detached snapshot"升级为显式契约（generation 语义、
  affinity 断言），覆盖 DataManager/temporal catalog 全部读路径，不再只修单点。

### #860 [high] WorkflowRunCoordinator 持锁 emit / 锁反转 — **still-valid，M0 修复**
- 复验（基点源码）：
  - `workflow_run_coordinator.cpp:283-300` `notifyRunStateLocked` 直接 `emit runStateChanged`，
    调用方 :401/:415/:617/:761/resumeRun-swap 均持 `m_mutex`。
  - `persistRunLocked`（持久化）也在锁内被多个 emit 邻近路径调用。
  - 同线程观察者槽若调用 `runs()/runForPipeline()/checkpointPathFor()` 等取同一把锁的接口 ⇒
    `std::mutex` 非递归 ⇒ 自死锁；跨线程连接则存在锁外回调拖住临界区的反转窗口。
- 修复方案（根因，非 recursive_mutex band-aid）：M0 将 coordinator 的
  "状态迁移 + 通知"拆分为**锁内排队、锁外 drain**（TaskCenter `flushPendingSignals` 同款模式），
  并对全部 `emit` 做锁下审计。M4 在此之上加序号/generation/迟到回调丢弃。

### #862 [high] TaskCenter 子任务准入饥饿/死锁 — **still-valid，M0+M1 修复**
- 复验（基点源码）：
  - `task_center.cpp:1628` `while ( m_active.total < globalMax ...)`：无 worker-thread 宽限。
  - `task_center.cpp:3078-3146` `waitForTask/waitForPipeline`：无 worker-thread 防护，
    默认 30min 条件变量等待。
  - JobEngine 层已有 #798 transient allowance（`m_transientAllowance`），但 TaskCenter 准入层
    是**独立**的 globalMax 闸门：父任务占满 globalMax 时，子任务在 WaitingResource 永不发射，
    而父任务 worker 线程阻塞在 waitForTask ⇒ 全池死锁。
- 修复方案：M1 显式 child-of-running-task 准入（transient、有界、公平轮转）+
  `waitForTask/waitForPipeline` worker-thread typed 拒绝/改造；回归测试用确定性 rendezvous，
  不靠睡眠。

### #876 [medium] resumeRun ghost run 泄漏 — **still-valid，M0 修复**
- 复验（基点源码）：resumeRun 先经 `startTrackedPipeline` 对**临时 runId** 广播
  `Running`（:401 notifyRunStateLocked），随后在 swap 中 :1055 `m_pipelineByRunId.erase`
  静默丢弃 ghost——checkpoint 文件删除了，但 WorkspaceService/SQLite 的 'Running' 记录
  永久滞留（无终态广播）。
- 修复方案：ghost 丢弃前必须经通知通道广播终态（Canceled，附原因）；配合 M0 的锁外
  通知队列实现。回归测试断言 resume 后不存在非终态 ghost runId 记录。

### #861 [high] RsScanPool 全局计数互扰 — **划界：UI 侧，Track 7**
- `src/app/widgets/rs_scan_pool.{h,cpp}` 属 Workbench/Track 7。本方向仅在根因落到
  execution/runtime seam（例如 TaskCenter 回调线程化契约）时提供窄接口，不抢 UI 所有权。
  PR 中声明划界理由。

## 其余 open issues（非本方向 ownership，逐条判定）

| # | 域 | 判定 |
|---|---|---|
| #848 terrain flow NoData 边界 | processing/hydrology | fixed-by-later-merge（8f6293bceb 本地，随 PR 上远端） |
| #849 SelectionContext UAF | app/workbench | fixed-by-later-merge（8f6293bceb 本地） |
| #850 VectorWriter move 丢事务态 | geospatial/vector | fixed-by-later-merge（8f6293bceb 本地） |
| #853 D8 float→int UB | processing | out-of-scope（scientific-processing 轨） |
| #854/#855 SAR sentinel/梯度 | operators/algorithms | out-of-scope（scientific 轨） |
| #856 spectral 负哨兵 | operators | out-of-scope |
| #857 display manager 泄漏 | app/display | out-of-scope（workbench 轨） |
| #858 widgets 死信号监听 | app/widgets | out-of-scope（workbench 轨）；若 teardown 需要执行面提供 run 句柄，走 M3 窄接口 |
| #859 canvas teardown race | app/workbench | out-of-scope（workbench 轨）；runtime 侧 shutdown drain 属 M4，接口契约在 ARCHITECTURE 声明 |
| #863-#865/#868 cartography solver | cartography | out-of-scope |
| #866/#877 MapSpec 条件 | agent/mapspec | out-of-scope |
| #867 RecipeCatalog | agent/harness | out-of-scope |
| #869-#872/#879-#882 help/schema/workbench 文案 | help/workbench | out-of-scope |
| #873 contracts +Inf 除数 | processing/contracts | out-of-scope |
| #874 raster reader 相等比较 | geospatial | out-of-scope |

## 关闭映射（PR 中逐条引用）

- #851/#852/#848/#849/#850 → fixed-by 8f6293bceb（随本 PR 进入远端）。
- #860/#862/#876 → fixed-by 本方向 M0 commits（回归测试同名引用）。
- #861/#859/#858 → 划界声明 + （如适用）窄接口交付。
