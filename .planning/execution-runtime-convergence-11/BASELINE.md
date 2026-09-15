# BASELINE — 启动审计事实（Phase 0，2026-09-15）

本文件记录本 track 启动时从 origin/GitHub 刷新的原始事实。Prompt 生成快照（ebcafb4d / PR #991 #992 open）已失效。

## Git 事实（刷新后）

- `git fetch origin --prune` 后：`origin/master` = **`a5b11b7f10fa010c1c060864fb427d777ba9a4aa`**（master 前移 `007e70cf → a5b11b7f`）。
- master 最近提交：
  - `a5b11b7f` fix: fail-closed fixes for review issues #994–#999 (#1000)
  - `1cea9892` Merge branch 'grok/dataset-foundry-benchmark-d19' (#992)  ← **#992 已合并**
  - `c5d4aafe` D18: Unified Mission Workbench — MissionContext + D14/D15/D17 mounts (#991)  ← **#991 已合并**
  - `77e178ac` fix(ci): resolve macOS/Windows compile errors in d17 and Win32 paths (#993)
  - 其后为 D19 系列（dataset foundry/benchmark）、`ebcafb4d`（#990 io OSR WKT）、#986–#989 各平台合并。
- Remote branches（按新近排序）：`origin/zcode/radiometric-spectral-workbench`、`origin/master`。无其他遗留分支。

## Open PR（刷新后仅 1 个）

- **PR #1008** `feat(spectral): Day 13 radiometric calibration, 6S atmospheric correction & spectral workbench`
  - head `zcode/radiometric-spectral-workbench`（4 commits，head commit `92fd8091`），base master，isDraft=false，mergeStateStatus=DIRTY（落后 master）。
  - 变更文件 41 个：`src/agent/spatial_tools/spectral_*`、`src/analysis/atmospheric|hyperspectral/*`、`src/app/widgets/spectral_profile_widget*`、`src/core/radiometric_state|spectral_library*`、`src/processing/algorithms/radiometric_calibration|spectral_indices|spectral_unmixing*`、`docs/adr/0158-*`、`.planning/radiometric-spectral-workbench/*`、测试 `tests/test_{continuum_removal,d13_*,fast_6s_*,radiometric_*,spectral_*}*`。
  - 共享注册文件：`.gitignore`(+5)、`tests/CMakeLists.txt`(+116)、`src/{agent,analysis,app,core}/CMakeLists.txt`。
  - **与本 track primary scope（src/runtime、src/jobs、src/operators/framework、tests/*execution*、tests/*chunk*、docs/execution）文件级交集：0。**

## Open issues（刷新后 7 个，均带 needs-triage，R2 review 产物）

| # | 域 | 标题 | 严重度 | 归属判断 |
|---|---|---|---|---|
| 1001 | io | io:clip 把 srcCrsOverride 当 targetCrs 用（silent wrong clip） | critical+P1 | src/operators/io — 非本 track scope |
| 1002 | workflow | makeRegistryNodeExecutor 不验证 artifact 文件存在（fail-open） | critical+P1 | src/workflow — 非本 track scope |
| 1003 | dataset | joinFeaturesBySampleId 把 JSON-null 当存在 | P1 | src/dataset — 非本 track scope |
| 1004 | agent | dataset:qa scan_capped 时 uniqueness 仍 Pass | P1 | src/agent — 非本 track scope |
| 1005 | georef | mapPickToLayerCrs 异常时返回未变换点 | P1 | georeferencer — 非本 track scope |
| 1006 | workflow/tests | PipelineRunCoordinator 软默认 syntheticExecute；scenario3c 未真正 registry bind | P2 | src/workflow — 非本 track scope |
| 1007 | dataset | dataset:qa 从不审计 CRS | P2 | src/dataset — 非本 track scope |

以上 7 条无一位于 src/runtime、src/jobs、src/operators/framework。处置：EVIDENCE.md `OUT_OF_SCOPE` 登记，不在本 track 修复（避免跨域并发冲突；这些是 R2 review 残留，属各自域 track）。

## 旧 backlog 核验（只读）

- `ISSUES.md` = D3 lab-content track 的平台算子缺口登记（T-1..T-3, S-1..S-2, H-1..H-3, C-1..C-2）。经查 master 现状：T-1（temporal_monitor scenes）与 T-2（temporal_regularize）、T-3（temporal_harmonic_breaks）、C-2（temporal_extract_regions）已在 Temporal Platform 10.0 实现（CHANGELOG [Unreleased] 段）；S/H/C 其余条目属域内容 track。**无一条属于 execution runtime 收敛域** → 不作为本 track backlog。
- `CHANGELOG.md`：确认 10.0 平台能力（Data Fabric、Verification、Spectral、Temporal 等）已合入；execution 域此前 track 为 `execution-concurrency-lifecycle-9`、`large-scale-execution-engine-10`（.planning 白名单可见）。
- `docs/agents/goal-template.md`（R1 草案后版本）确认 /goal 协议、budget、build 资源硬约束（`CMAKE_BUILD_PARALLEL_LEVEL=2`、`CTEST_PARALLEL_LEVEL=1`、QT_QPA_PLATFORM=offscreen）。

## 代码结构审计结论（三路只读 Explore 审计，2026-09-15）

### src/runtime（Qt-free SHARED lib `sicnu_runtime`）
- `chunk/`：BoundedChunkQueue（容量硬界）、ChunkPipeline（线性 N 段流水线；**consumer-abort 静默正常返回的缺陷**，ChunkGraph 已修）、ChunkGraph（DAG、join 对齐、no-deadlock unwind）、DiskTileStore（fail-closed tile 序列化 + BoundedWriteGate 写背压）、memory_planner（advisory；**F-A-13 诚实缺口：漏算 per-stage in-hand tile，不可作硬 gate**）、multi_pass_reduction（零采用者）、ScratchRegistry（预算租约、.part→rename、digest sidecar；Windows fsync no-op；sweepStale 仅限启动期）、TileCheckpoint（operatorIdentity/inputIdentity 双门 + completedTiles 位置；原子 tmp+rename 保存；cancel→remove）、TileSpec（15 字段 + buildTileGrid）。
- `observability/`：execution_telemetry（8192 ring + counters；**QueueWait/AdmissionWait/ResourceWait/ChunkProgress/TilesProcessed 词汇已定义但从未发射**；ScopedTelemetrySpan 零用户）、trace（NDJSON span、ring/file sink、TraceContext，消费广泛）、fault_point/fault_registry（测试注入）、diagnostic_report（**无生产发射者**）。
- `worker/worker_protocol.h`（header-only v1：run/cancel/shutdown/ready/progress/ack/heartbeat/result/error；**无 lease/TTL/poison 概念**）。实现：src/cli/sicnu_worker_main.cpp、src/processing/framework/local_worker_{host,pool}。
- `gpu/gpu_plane`：ModelSessionPool + VRAM 预算/OOM 阶梯。
- **关键事实：TileCheckpoint / ScratchRegistry / DiskTileStore / BoundedWriteGate / multi_pass_reduction / ChunkGraph 生产消费者为零（仅测试）；ChunkPipeline 唯一生产消费者是 feature-flag 关闭的 fused_chain.cpp，且其 cancelFlag 声明后从未接线（死代码）。**

### 执行权威图（src/jobs + processing framework + workflow）
- 主脊柱：surface → ExecutionPlane → TaskCenter（TaskStatus 超集 + DAG + admission + auto-retry 0..3 + execution cache）→ JobEngine（JobState + 线程池 + 优先级桶）→ operator。WorkflowRunCoordinator 是唯一持久 run 状态机（原子 checkpoint、startup 恢复、resume 跳过已验证输出）。LocalWorkerPool 自我声明"NOT a second job scheduler"。
- Cancel 链完整：TaskCenter→JobEngine→RSOperatorContext cancel flag→operator throwIfCancelled；ChunkPipeline 支持轮询 flag 但 fused_chain 未接。
- Publication：OutputCommitter 事务性 publish-then-swap + PartialOutputGuard（operator 级 fail-closed 删除）+ ExecutionPlane commit-once cache。
- 重复点：ExecutionPlane 第三个状态枚举（镜像）；transient 允许量在两层各 8；ChunkedProcessor 内自有 QThreadPool（job 内 fan-out，预算注释过期）。

### operators/framework
- RSOperator 合同：run/schema/metadata/memoryPolicy/streamingHaloPixels(从未被覆写)/determinism/executionEstimate；RSOperatorContext：progress 节流 + cancel flag/callback + workDir（**未接 ScratchRegistry**）。
- RSOperatorError 类型化错误码（append-only 分类法）；artifact_digest = SHA-256（模型工件专用；checkpoint 用 FNV-1a-64——分族故意）。
- **无 ChunkTask/TileRun 合约**：每个流式算子手写 tile 循环（~24 文件用 GdalBlockStream）；halo 语义散落在注释/manifest；chunk 异常族与 RSOperatorError 之间无类型化翻译层；两套 cancel/progress 体系无桥。

### 结论：本 track 的真实缺口（与 GOAL Why-this-track 判断一致）
1. 库级能力未接线成 substrate（resume/scratch/disk-store/gate 零生产使用）。
2. 唯一生产消费者 fused_chain 有死 cancelFlag（P1 级 fail-open 取消缺口）+ ChunkPipeline consumer-abort 静默成功（P1 级 fail-open）。
3. 无统一 chunk 调用合约、无桥接层、无 adoption kit。
4. memory_planner 不能作硬 gate（F-A-13），无统一资源治理面。
5. worker 无 lease/poison 语义。
6. 遥测词汇已定义未发射，无 per-chunk span，DiagnosticReport 无发射者。
7. 缺 hermetic 规模/故障验证套件。

## 构建事实

- CMakePresets：configure presets = `dev-default`(bin `build-dev`, Debug), `ci-fast`, `ci-full`, `sanitizer-debug`, `release-package`。无名为 `build-dev` 的 preset——GOAL 所指"build-dev"即 `dev-default` 的 binaryDir（DECISIONS D-003）。
- `.gitignore` `.planning/*` 白名单模式：每 track 三行 append（`!.planning/<slug>/` + `!<slug>/*` + `!.planning/<slug>/*.md`）。
