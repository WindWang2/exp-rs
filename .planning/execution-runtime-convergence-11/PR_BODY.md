# Scientific Execution Runtime Convergence 11.0

> **Local evidence only; no online CI dependency.**

## Baseline & dedupe

- Baseline `origin/master@a5b11b7f10fa010c1c060864fb427d777ba9a4aa`（启动审计时唯一 open PR 为 #1008 spectral，与本 diff 零文件交集；#991/#992 已在基线内合并）。
- Open issues #1001–#1007 均为 io/workflow/dataset/agent/georef 域 R2 残留，与本 track 无重叠，登记于 EVIDENCE `OUT_OF_SCOPE` 未修（避免跨域冲突）。
- 本地并行 worktrack `zcode/geoai-promptable-foundation-platform-11` 只写 `app/lib/modelops/**`（Python），零交集。

## Why

10.0 的执行原语（ChunkGraph、TileCheckpoint、ScratchRegistry、DiskTileStore、write gate、遥测词汇表）已存在但**未接线**：checkpoint/scratch/disk-store/gate/multi-pass 零生产消费者；唯一生产消费者 fused_chain 的取消标志是死代码；ChunkPipeline consumer-abort 静默返回成功（fail-open）；每算子手写 tile 循环（~24 处），halo/cancel/错误信封两套体系无桥。本 PR 把这些原语收敛为可采用的执行底座——**不引入第二 scheduler**。

## 架构决定（详见 .planning/execution-runtime-convergence-11/DECISIONS.md D-001..D-019）

1. 合约在 Qt-free `src/runtime`，RSOperatorError 翻译在 `src/operators/framework`（依赖方向 operators→runtime 不变）。
2. resume 采用 append-only 行式 journal + 既有 TileCheckpoint v1（格式不破坏）+ run 级 PUBLISHED marker（tmp+rename）。
3. worker lease/poison 为宿主侧纯策略对象（注入时钟），协议零改动；池保持 fail-loud 不自动重试。
4. planner 修复方向为保守上界（(2S+2)·I in-hand 项），宁可拒算不可低估放行；随机占用模拟器为独立 oracle。
5. reference adopter 仅在测试注册（不与 20 个域 track 抢算子文件）。

## 实际交付（WP A–H 全部）

- **A** ChunkPipeline `ChunkConsumerAborted` fail-closed；fused_chain 取消桥+类型化错误翻译；源扫描架构网（36 文件冻结 allowlist，含 QThread::create/pthread/CreateThread 形态）+ JobEngine 1:1 权威行为测试。
- **B** `tile_run_contract`（partition digest 稳定+全字段漂移敏感，独立 FNV oracle）；错误信封全表映射（新码 CorruptArtifactData=2005、ResourceBudgetExceeded=4103，append-only）；`ChunkCancelBridge`。
- **C** `ResumableTileRun`：R1–R6 恢复律；**真实子进程硬杀**（`_Exit(70)`）后已提交 tile 零重算、输出 byte-equal、恰好一次出版；撕裂 journal 尾截断/中段损坏 fail-closed/tile 损坏自愈/身份漂移 wipe。
- **D** planner 上界修复 + `ExecutionGovernor`（RAM/scratch/write-gate 一体 + 泄漏检测 → 首个生产 exp.diag.v1 发射）。
- **E** `WorkerLeaseTracker`（lease 过期/毒丸隔离/有界接管）+ LocalWorkerPool 接线（idle 扫描隔离 + 读循环 lease 过期升级）。
- **F** 遥测词汇表落地：queue-wait/chunk-progress/stage 时长采样发射（≤ tiles/rate+O(1)）、tiles_processed/cache 计数器精确、RunResumed。
- **G** `runChunkedOperator` adoption kit（Resumable/Pipeline 双模式 byte-equal、RAM 硬准入、双形态取消桥）+ ADOPTION_GUIDE + synthetic reference adopter。
- **H** 10^6 逻辑 tile 纯算术、种子化取消风暴、间歇崩溃重置至恰好一次出版、opt-in 100k journal 往返。

## 兼容性

- 旧 API 零破坏：新类型全部 additive；错误码 append-only；TileCheckpoint v1 格式不变；ChunkPipeline 唯一行为变化是 consumer-abort 从静默成功改为抛 `ChunkConsumerAborted`（:ChunkCancelled 子类）——这是缺陷修复（fail-open→fail-closed），唯一生产消费者 fused_chain 从不返回 false。
- LocalWorkerPool 新增 lease/poison 判定：连续 3 次 operator 级失败才隔离（默认阈值），成功清零；fail-loud 契约不变。

## Local tests

（Phase 8 回填：每套件命令与 exit code，连续两遍。）

## 资源证据

（见 PERFORMANCE.md：上界模型表、逻辑规模、遥测界。）

## Review findings 与处置

两轮独立只读对抗审查（resume 协议轴 + 治理/lease/adoption/测试可信度轴）：P0=1（identity key `:` 分隔符 Win32 非法）→ 修复；P1=3（空 journal 头崩溃陷阱、planner 上界低估 2 tiles/stage、_getpid POSIX 破坏）→ 全部修复；P2/P3 全部 disposition（REVIEW_LOG.md）。修复后 **P0=0、P1=0**。

## Known limitations

- Windows fsync 为 no-op：resume 原子性边界= rename（沿用 ScratchRegistry/TileCheckpoint 既有契约；D-016）。
- 子进程崩溃测试钩子使用 ANSI Win32 API：非 ASCII 临时路径宿主上可能误触发（精确 exit code 70 断言使误触发可诊断；本宿主为 ASCII 路径）。
- 生产域算子尚未迁移 adoption kit（有意：由各域 track 按 ADOPTION_GUIDE 进行；本 track 只交付 synthetic adopter）。
- ChunkedProcessor 内 QThreadPool 注释过期（P3，src/processing 域，登记未修）。

## Follow-ups

- 各域 track 按 `docs/execution/ADOPTION_GUIDE.md` 迁移高频大算子到 `runChunkedOperator`。
- `Streaming/GlobalReductionStreaming/ExternalMemoryStreaming` memory policy 的生产采用（配合 multi_pass_reduction）。
- governor 预算的配置面（settings/env）暴露与 GUI 诊断面板。
