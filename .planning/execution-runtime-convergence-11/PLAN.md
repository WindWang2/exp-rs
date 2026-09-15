# PLAN — Scientific Execution Runtime Convergence 11.0

Branch `zcode/execution-runtime-convergence-11` off `origin/master@a5b11b7f`（2026-09-15）。
审计结论（BASELINE.md）：10.0 底座原语存在但**未接线成 substrate**。本 track 不造第二 scheduler，而是把既有权威（TaskCenter→JobEngine 脊柱、WorkflowRunCoordinator 持久 run、OutputCommitter 出版）与既有库级能力（chunk runtime、checkpoint、scratch、遥测、fault 注入）之间的缺口收敛为一个可采用的执行底座。

## 设计总览（一张图）

```
surface (GUI/CLI/agent/workflow)
  └─ ExecutionPlane ── TaskCenter (admission, DAG, auto-retry)   ← 唯一 task authority（不变）
        └─ JobEngine (thread pool, JobState)                     ← 唯一 job scheduler（不变）
             └─ RSOperator::run(params, RSOperatorContext)
                   └─ [NEW] adoption kit: chunked::runChunked(ctx, TileRunSpec, kernel)
                         ├─ ChunkInvocation contract（partition identity / halo / determinism / error envelope / cancel bridge）
                         ├─ ResumableTileRun driver（journal + checkpoint + scratch + digest → exactly-once publish）
                         ├─ ExecutionGovernor（RAM/scratch/write-in-flight 预算与降级阶梯，leak 检测）
                         ├─ ChunkPipeline/ChunkGraph（有界流水线，取消轮询，per-chunk telemetry）
                         └─ worker 层（protocol v1 + lease/poison tracker，LocalWorkerPool 集成）
```

## Phase 计划（映射 GOAL phases 与 work packages）

### Phase 1（=WP-A）权威收敛与 fail-closed 修复
1. `docs/execution/CURRENT_ARCHITECTURE.md`：authority/seam 真值图（从审计产出）。
2. 修复 `ChunkPipeline` consumer-abort 静默正常返回（fail-open→fail-closed）：新增 `ChunkConsumerAborted` 异常并在 `run()` 重抛；更新既有测试预期（行为变化即本修复的目的，记 DECISIONS）。
3. 修复 `fused_chain.cpp` 死 cancelFlag：桥接 RSOperatorContext 取消 → `setCancelFlag`；chunk 异常族 → RSOperatorError 类型化翻译（Cancelled 等）。
4. `tests/test_execution_authority_11.cpp`：
   - 源扫描架构测试（沿用 test_algorithm_organization 先例）：禁止在 allowlist 之外新增 scheduler 形态（QThreadPool/QtConcurrent::run/new std::thread 的所有权清单）；
   - 行为测试：JobEngine↔TaskCenter 单权威不变量（运行中任务 1:1 映射 job 记录）；cancel 链 JobEngine→context flag→throwIfCancelled。
   - ChunkPipeline consumer-abort 抛错 + 取消传播测试。

### Phase 2（=WP-B）统一 ChunkTask/TileRun 合约
`src/runtime/chunk/tile_run_contract.h`（operator-independent，Qt-free）：
- `TileRunIdentity`：{operatorIdentity, inputIdentity, partitionDigest}——partitionDigest 对 tile 网格参数（rasterW/H、tileW/H、halo、bands、bandOffset、timeIndex、totalTiles）做 FNV-1a-64 规范哈希（复用 tile_checkpoint 哈希族）。
- `TileRunSpec`：identity + halo 语义（core/halo 边界契约）+ determinism grade + 输出 artifact 描述 + 每tile kernel 类型擦除合约。
- 错误信封映射表：ChunkCancelled→Cancelled、ChunkConsumerAborted→Cancelled(PartialOutput)、ChunkCorruptTile→CorruptTile(typed)、ScratchBudgetExceeded→PolicyRefused(ResourceBudget)、其余→InternalError；实现于 operators/framework 侧 `chunk_error_bridge`（因为 RSOperatorError 在那层）。
- `ChunkCancelBridge`：atomic flag ↔ predicate 双向（operators/framework 侧）。
测试 `tests/test_chunk_contract_11.cpp`：identity 稳定性/漂移敏感性 known-answer；错误映射全表；cancel bridge 行为。

### Phase 3（=WP-C）真实 crash-safe resume（核心）
`src/runtime/chunk/resumable_tile_run.{h,cpp}`：
- `RunJournal`：append-only 每tile提交记录（tileIndex、finalPath、payloadDigest、bytes），O(1) 内存；损坏行→截断到上次完好记录（fail-closed：被截断的 tile 重算）。
- `ResumableTileRun::execute()`：load checkpoint（identity 双门，mismatch→fresh+清扫旧 run scratch）→ 重放 journal → 逐 tile：kernel→写 provisional→seal digest→finalize(rename)→journal append→checkpoint 周期保存 → 全部提交后 exactly-once publish（run 级 marker：`PUBLISHED` 状态原子落盘；重入时已 PUBLISHED 直接返回成功且不重算）。
- 取消→checkpoint 保留（可恢复）；失败→fail-closed（provisional 清理、无半出版）。
- Windows fsync no-op 事实下的原子性边界声明：rename 为原子边界（记 PERFORMANCE/DECISIONS）。
测试 `tests/test_chunk_resume_11.cpp`：
- in-process 模拟崩溃（new controller 对象对磁盘状态恢复）；
- **真实子进程崩溃**：测试二进制自重执行（env `SICNU_TEST_CRASH_AFTER=<N>`）跑 N tile 后 `_Exit(70)`；父进程 resume，断言：已提交 tile 零重算（kernel 计数器）、未提交 tile 重算、输出与全量直跑 byte-equal、publish 恰好一次；
- 身份漂移（改 inputIdentity/operatorIdentity）→ 拒绝复用（fresh）；
- journal/checkpoint/tile 文件损坏矩阵 → 重算或 fail-closed，绝不静默错数据。

### Phase 4（=WP-D）资源治理与 admission
- `memory_planner` F-A-13 修复：模型加入 per-stage in-hand tile 项（(S+1)·cap + S + 1 + 全局态），使估计成为保守上界；`joinInputCount` 语义修正为保守应用于全 stage（上界方向），测试用暴力占用模拟验证 plan ≥ 任意调度下观测峰值。
- `src/runtime/exec/execution_governor.{h,cpp}`：统一预算面（ramBytes via planner、scratchBytes via ScratchRegistry、writeInFlightBytes via BoundedWriteGate、workerSlots）；`admit(spec)` → Admit/ReduceConcurrency/Spill/Refuse 阶梯；泄漏检测：析构时 outstanding 租约>0 → DiagnosticReport + telemetry 计数（fail-closed 记录，测试断言）。
测试 `tests/test_execution_governor_11.cpp`：预算超限→typed 拒绝；降级阶梯顺序；溢出安全（saturating）；泄漏检测；压力（小预算下有界执行）。

### Phase 5（=WP-E）worker lease 与故障隔离
- `src/runtime/worker/worker_lease.{h,cpp}`：`WorkerLeaseTracker`（注入时钟）：per-worker lastHeartbeat/jobId/连续失败计数；`onHeartbeat/onJobResult/now` → 决策 {Healthy, ExpireAndTakeover, QuarantinePoison}；TTL/毒丸阈值/接管重试上限可配置。协议零破坏（心跳/进度帧即 liveness 信号；不加新 op，符合 protocol v1 extension rule——若需 lease 续期信息，用可选字段）。
- LocalWorkerPool 最小接线：宿主侧心跳超时→kill+回收+有界接管重试；连续失败→隔离不再派新 job（append-only 小改）。
测试 `tests/test_worker_lease_11.cpp`：注入时钟的 lease 过期/接管/毒丸/恢复；bounded retry 用尽→fail-closed typed error。

### Phase 6（=WP-F）per-chunk observability
- ChunkPipeline/ChunkGraph 发射遥测：QueueWait（pop 等待时长）、ExecutionStart/End（per stage 采样）、ChunkProgress + `TilesProcessed` 计数器；采样策略：每 tile 计数器必发（原子计数，无内存增长），span/事件按 1/N 采样 + 首/末 tile 必发（默认 N=16，Config 可调），保证事件总量 ≤ tiles/N + O(stages)。
- ResumableTileRun 发射 RunResumed/CacheHit(checkpoint attach)/retry/cancel latency；失败时产出 DiagnosticReport（首个生产发射者）进入错误 details。
测试 `tests/test_execution_telemetry_11.cpp`：事件上限 ≤ ring 容量；采样界；计数器精确 = tile 数；cancel latency 可观测。

### Phase 7（=WP-G）兼容层与 adoption kit
- `src/operators/framework/chunked_run.{h,cpp}`：`runChunkedOperator(ctx, TileRunSpec, TileKernel)`——一次接入获得：有界流水线/顺序可恢复执行（可配）、halo 声明、NoData hook、cancel/progress 桥、错误信封、遥测、resume、原子出版。旧 API 零破坏（纯新增）。
- Reference adopter：`tests/test_chunk_adoption_11.cpp` 内注册的 synthetic 算子（不入 builtin init 清单，不与域 track 抢文件）+ known-answer 数值真值（独立公式，不复用被测实现）。
- `docs/execution/ADOPTION_GUIDE.md` + `CAPABILITY_MATRIX.md` before/after。

### Phase 8（=WP-H）规模、故障与回归验证
- `tests/test_execution_scale_fault_11.cpp`：
  - 逻辑规模：1e6 tile 计划的身份/网格/journal 数学不物化（O(1) 内存断言）；默认 bounded 规模全真执行；
  - 故障矩阵：journal 截断、tile 字节翻转、checkpoint 损坏、scratch 预算耗尽（磁盘满模拟）、取消风暴（随机点取消×多次）、子进程崩溃×多次间歇恢复至完成；
  - 不变量全部精确（计数/digest/byte-equal），无墙钟门。
- `PERFORMANCE.md`：资源模型 + 实测 RSS/队列上限（本地测量，非 CI）。

### Phase 7/8 review（GOAL Phase 7）
主 agent 全量 diff review → 独立只读 reviewer（架构/数值/并发/测试可信度轴）→ P0/P1 全修 → targeted gate ×2。

### PR（GOAL Phase 8）
rebase origin/master → 双遍 targeted 验证 → `git diff --check`、冲突标记/secret/生成物 zero-diff 扫描 → push（不 force）→ `gh pr create`（不 merge）。

## Commit 策略

每 Phase ≥1 原子 commit；共享注册文件（tests/CMakeLists.txt、.gitignore、CHANGELOG）的改动尽量并入对应功能 commit 的 append-only 段。每次 commit 后记录 `git status --porcelain` + 验证命令与 exit code 到 EVIDENCE.md。
