# MILESTONES — 状态与证据

| 里程碑 | 状态 | 证据 |
|---|---|---|
| M0 基线 | done | Windows 全量首建+测试通过（master 上该平台从未绿过执行面套件） |
| M1 protocol | code-complete | worker_protocol.h 可选 caps/ack/code/outputs；sicnu_worker caps+ack+codes+outputs 清单；host/pool stderr 环形诊断+progress 透传；test_worker_host 新增 4 组用例 |
| M2 worker wiring | code-complete | worker_execution_route.{h,cpp}（off/auto/require、configured-mode fail-closed、隔离槽位上限）；LocalWorkerPool 每-worker 线程亲缘 + 槽位自愈；TaskCenter staging/flush/shutdown 接线；SICNU_WORKER_EXECUTION/PROGRAM/MAX_CONCURRENT |
| M3 admission | code-complete | Windows RSS 采样修复（psapi，门禁上线）；budget2 +tempDisk 维度；TaskCenter tempDisk/VRAM 门 + ioHeavy 并发门（默认全关）；descriptor 新增 executionPreference/ioHeavy + 序列化 |
| M4 cancellation | code-complete | cancel ack 证据链（M1）+ escalation 梯子保持；测试：acked-cancel（host/pool）、queued-cancel-under-hold |
| M5 crash/resume | code-complete（retry 部分） | markTaskFailed 有界 transient auto-retry（复活原任务、DAG 保持、上限 clamp 0..3 默认 1、TaskAutoRetries 遥测）；moved-output/changed-operator resume 待做 |
| M6 committer | code-complete（agent 部分） | stampPlanResultProvenance：agent plan 两路径注册 governed asset + makeWorkflowDerivation + lineage；image_fusion #617 已有 guard（记录为已知限制） |
| M7 cache | code-complete | serve 后目标文件尺寸验证（TOCTOU 关闭）；execution_identity_resolver seam（本地默认=现行为）；磁盘 GC 配额已存在（SICNU_ARTIFACT_CACHE_MAX_GB 默认 8GB，避免重复开发） |
| M8 concurrency | code-complete | waitForTask/waitForPipeline 审计结论：QWaitCondition 等待期释放锁、无持锁等待缺陷（记录 REVIEW_LOG）；池析构竞态关闭（m_destroying 拒绝新 run） |
| M9 fault/stress/perf | code-complete（测试部分） | tests/test_execution_plane_7.cpp：retry×2、dag-保持、路由 e2e、fail-closed、tempdisk/VRAM hold+never-starve、RSS parity、queued-cancel、10k rapid、shutdown；RUN_SERIAL |
| M10 review/PR | in_progress | 评审 0×P0/2×P1/10×P2 → P1 全修+P2 处置 → 复测 9/9+12/12+8/11 |

## M0 记录
- 2026-09-09 worktree 建立 `../exp-rs-execution-plane-runtime-7`，分支
  `feat/execution-plane-runtime-7` 自 master `2041f6fa`。
- 环境：Windows 10.0.26200, VS2022 Community, Qt 6.8.0 msvc2022_64,
  vcpkg toolchain, Ninja, win_flex/bison (C:/deps/winflexbison)。
  构建目录 build-dev（Debug, ENABLE_TESTS=ON），
  CMAKE_BUILD_PARALLEL_LEVEL=2, CTEST_PARALLEL_LEVEL=1。
- 配置两次失败修正：BISON/FLEX 路径（win-build worktree 同款）。
- 基线构建 `test_worker_host`+`sicnu_worker` 目标后台运行中（qgis_core
  依赖链首建）。完成后记录 ctest 基线。

## 关键设计决定（实现中落定）
1. worker 路由 = TaskCenter staging 时的 per-job executor 注入（JobEngine
   零改动，解析链优先级天然支持）。
2. 池线程模型：整池单 owner → 每-worker owner（JobEngine 多线程并发驱动
   QProcess 亲缘所需）；外来孤儿 worker 槽位可被强制回收。
3. 路由模式语义：configured-mode（非 started-state）——require 在池不可用
   时 fail-closed 而非静默回落；off 默认=master 行为。
4. transient auto-retry 只认 worker crash/timeout/spawn 前缀（operator
   错误/校验/取消永不自动重试）；复活原任务而非新建（DAG 完整）。
5. 不做：worker 进程 Windows Job Object（记为已知限制，stdin-EOF 契约 +
   kill 兜底已保证 GUI 不受 worker 崩溃影响）；moved-output/changed-operator
   resume（M5 剩余，见 FINAL_REPORT）。


## 复测证据（P1/P2 修复后, 2026-09-10）
- run_all_ep7.cmd（逐用例独立进程）：**9 passed, 0 failed**
- test_worker_host：**All tests passed (56 assertions in 12 test cases)**
- run_fence.cmd：**8 passed, 3 failed**（3 个为既有 Windows 平台限制）
- 性能：10k 短任务 drain **83.294 s**（Debug, --durations）
