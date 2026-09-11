# BASELINE — Execution / Concurrency / Lifecycle Platform 9.0

基线建立时间：2026-09-11（本文件所有结论均基于当日实测，不沿用历史规划的断言。）

## 1. 仓库与分支状态（实测）

- `origin/master` = `132da5e998`（Merge PR #847 geospatial-data-fabric-8）。
- 本地 `master` = `8f6293bceb` = origin/master + 1 个未推送提交
  `fix(core): resolve P0 defects in terrain flow, selection context, vector writer, task center, and data manager (#848-#852)`。
  - 该提交是针对 #848–#852（含本方向 P0：#851 TaskCenter self-deadlock、#852 DataManager 跨线程撕裂读）的
    P0 修复，附带回归测试（test_task_center/test_temporal_workspace/test_selection_context/
    test_terrain_foundation5/test_io_vector_contract 各有新增用例）。
  - **本方向分支基点 = `8f6293bceb`（本地 master）**，理由：
    1. 它严格领先 origin/master 且只含 P0 修复，属于本方向 M0 范畴；
    2. 从 origin/master 切分支会与该修复在 task_center.cpp 上必然冲突；
    3. 该提交随本方向 PR 进入远端，避免孤儿修复。
  - 风险声明：若另一个并行 track 先把 8f6293bceb 推上 master，本 PR 自动退化为纯 9.0 增量（git 自动处理）。
- 主工作区（main worktree）存在大量**未提交**改动（前一会话针对 #853–#881 的快速修复草稿，
  含 `recursive_mutex` band-aid 等）。**本方向不以这些未提交草稿为基线**；其中被复验为真实缺陷的
  部分按本方向的根因方案重做（见 ISSUE_TRIAGE.md）。
- Open PR：**0 个**（gh pr list --state open = []）。
- Open issues：35 个（#848–#882，全部带 bug 标签）。
- Remote branches：`origin/feat/*-5/6/7/8` 全部为已合并 PR 的残留（对应 PR #818–#847 均 merged），
  无领先 master 的独立未合并提交（逐支核对近期 merge-base）。无任何 `-9` 分支。

## 2. 最近 merged PR（30 个，抽样核对）

#847 geospatial-data-fabric-8、#846 scientific-processing-8、#845 professional-workbench-8、
#844 plugin-platform-8、#843 dataset-experiment-mlops-8、#842 spatial-scientist-harness-8、
#841 **execution-plane-8**（本方向前身）、#840 cartography-platform-8、#839 verification-platform-8、
#837 model-runtime-8……（详见 git log；全部已于 2026-09-10/11 合并。）

### 8.0 Execution Plane 已进入 master 的能力（来自 #841 + 7.0 #828）

- TaskCenter 增量准入：ActiveCounters、单一状态迁移缝 `setTaskStatusLocked`、
  就绪堆 `(priority, epoch, taskId, serial)`、有界扫描（`kAdmissionScanFloor`，4×globalMax）。
- JobEngine 优先级桶 `m_queueBuckets` + 独占 FIFO + #798 worker 子任务 transient allowance
  （`m_transientAllowance`，kMaxTransientWorkers=8，thread_local `t_isWorkerThread`）。
- Worker containment：`worker_process_guard`（POSIX 进程组 SIGTERM→SIGKILL；Windows Job Object 编译级）。
- 瞬态错误分类器 `isTransientExecutionError` + 有界自动重试（0..3）。
- Resume 3.0：StepPlan.operatorImplStamp、fail-closed 身份门、moved-output 摘要重水化。
- 远端身份解析 `remote_identity_resolver`（强 ETag、Qt-free、TTL 有界会话缓存）。
- Trace：TaskCenter admitted/held/retry/cancel/terminal/cache + coordinator resume 事件。
- 完成回调通道 `addTaskCompletionCallback`（event-loop 无关、exactly-once、锁外触发）。

## 3. 本机构建环境（实测）

- Linux 6.18 x64，16C / 62GB，GCC 16.2.1，ninja，ccache，Qt 6.11（/usr/lib/cmake/Qt6），
  GDAL 3.13.3。
- **构建事实**：origin/master 上的 `src/geospatial/metadata/canonical_metadata.cpp` 在本机 GDAL 3.13.3
  存在 `GUInt64*`→`size_t*` 隐式转换错误，需 `-fpermissive` 才能编译（主构建目录即使用
  `CMAKE_CXX_FLAGS=-fpermissive`）。src/geospatial 非本方向 ownership，本 worktree 采用与主构建
  一致的 `-fpermissive` 配置，不修改该文件。
- 本 worktree 构建配置：Release + Ninja + ccache，`-j4` 上限（资源保护），测试 `-j1`。

## 4. Issue 复验结论摘要（详见 ISSUE_TRIAGE.md）

| Issue | 最新基点状态 | 判定 |
|---|---|---|
| #851 flushPendingLaunches self-deadlock | 已修（8f6293bceb：cancel 移出锁外 + 回归测试） | fixed-by-later-merge（本地） |
| #852 DataManager 跨线程撕裂读 | 已修（8f6293bceb：detached snapshot + 回归测试） | fixed-by-later-merge（本地），M2 需将其契约化 |
| #860 coordinator 持锁 emit | **仍复现**（notifyRunStateLocked 持 m_mutex emit，:297；caller :401/:617/:761） | still-valid，M0 修 |
| #862 TaskCenter 子任务准入饥饿 | **仍复现**（processNextQueuedTasks :1628 无 worker 宽限；waitForTask :3078 无 worker 防护） | still-valid，M0/M1 修 |
| #876 resumeRun ghost run | **仍复现**（:1055 静默 erase，无终态广播；startTrackedPipeline 已广播 Running） | still-valid，M0 修 |
| #861 RsScanPool 全局计数互扰 | src/app/widgets（UI），根因若在 runtime 需窄接口 | 与 Track 7 划界，本方向只修 execution/runtime 侧 |
| 其余 #848-#850/#853-#882 | 属 processing/cartography/workbench/help 等 track | out-of-scope（逐条见 ISSUE_TRIAGE） |

## 5. Baseline 测试（构建完成后回填）

（待回填：test_task_center / test_job_engine / test_worker_host / test_workflow_run_coordinator /
test_execution_plane_7 / test_execution_plane_8 / test_concurrency_stress 在基点的通过记录。）
