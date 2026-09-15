# /goal — G01 · Scientific Execution Runtime Convergence 11.0

/goal  target-agent=zcode  model=GLM-5.3  budget=400000000  subagents=unbounded  ci=none  autonomy=full  defaults=best

> **Mission:** 统一有界、可恢复、可观测的科学执行底座
> **Target model:** GLM-5.3（强模型；适合跨层架构与高难科学/并发裁决）
> **Branch:** `zcode/execution-runtime-convergence-11`
> **Worktree:** `../exp-rs-execution-runtime-convergence-11`
> **Terminal state:** 独立 PR 已创建；不 merge；不等待在线 CI。

## Prompt-generation snapshot（只用于启动审计，不是固定基线）

本 Prompt 生成时（2026-09-15）观测到：
- `origin/master` = `ebcafb4d02ec3522eaaa4b3b62b1082c36280ffb`。
- open PR #991 `grok/unified-mission-workbench-d18`：MissionContext / IR2 dock / workflow mounting；head `8dbd6bde1aa8b0538c2e7f74bed3ba776db35a1f`。
- open PR #992 `grok/dataset-foundry-benchmark-d19`：Dataset Foundry / Benchmark；head `08264801a079efff683cd2446a0acee4c2153448`。
- 当时无独立 open issues；`ISSUES.md` 是旧 D3 backlog，其中多数条目已被后续 10.0 PR 修复，**禁止把它当实时 backlog 直接实施**。
- 最近 master 已合入 D14 geometric、D15 classification/change、D16 temporal、D17 workflow，以及 Data Fabric / Verification / Spectral / Execution / EO Model / Workbench 等 10.0 平台能力。

**启动时必须完全刷新这些事实。任何 SHA/PR 状态发生变化，以启动时 GitHub/origin 事实为准。**

## Why this track now

10.0 已有 ChunkGraph、内存规划、ScratchRegistry、DiskTileStore、tile checkpoint、VRAM bridge，但它们仍主要是底座能力；下一阶段应把执行权威、恢复协议、资源治理、故障隔离和遥测收敛为一个可供所有科学算子采用的稳定 execution substrate，而不是再造第二 scheduler。

## Work packages

| ID | Package | Required deliverables |
|---|---|---|
| A | 执行权威与状态机收敛 | 逐行审计 TaskCenter/JobEngine/runtime/chunk/operator framework；画出 authoritative scheduler、task state、cancel、retry、checkpoint、publication 的单一真值图；消除重复/旁路状态机。 |
| B | 统一 ChunkTask/TileRun 合约 | 定义 operator-independent 的 chunk invocation、partition identity、input/output artifact、halo、determinism、cancel、error envelope；兼容现有 operator，不破坏旧 API。 |
| C | 真实 crash-safe resume | 把现有 tile checkpoint 从"库级能力"推进到进程崩溃/重启后的可验证恢复：identity gates、partial publish attach、digest、stale cleanup、exactly-once publication；失败必须 fail-closed。 |
| D | 资源治理与 admission | 统一 RAM/VRAM/scratch/open-files/I/O writers/worker slots 的预算与降级阶梯；实现配置面、默认值、溢出安全、资源泄漏检测和压力测试。 |
| E | 本地多 worker 与故障隔离 | 在不引入第二 scheduler 的前提下完善 worker lease、heartbeat、poison task、worker crash/restart、cancellation propagation、bounded retry；保持单机优先。 |
| F | per-chunk observability | 结构化 span/metric/event：queue wait、read/compute/write、cache/checkpoint hit、spill、retry、cancel latency；必须有采样/上限，不能让遥测反过来 OOM。 |
| G | 兼容层与 adoption kit | 交付 adapter/guide/test harness，使现有 rs/io 算子可逐步采用新执行底座；本 track 只做 synthetic/reference adopter，避免与 20 个领域 track 抢具体算法文件。 |
| H | 规模、故障与回归验证 | 百万逻辑 tile、强制 crash、磁盘满/损坏、低内存、取消风暴、worker 故障、resume 等 hermetic 场景；建立精确不变量而不是墙钟门。 |

## GOAL Loop Oracle（未满足不得结束）

1. 没有第二 scheduler/第二 task authority；架构测试能证明 authority 唯一
2. crash/restart 后已提交 tile 不重复计算且身份漂移时拒绝复用
3. 所有队列/缓存/scratch/遥测均有硬上限和故障测试
4. targeted execution suites 连续两次通过，且 resource/cancel/resume 证据写入 EVIDENCE.md
5. 最终 diff 不触碰仍开放的 #991/#992 独占文件，除非它们已合并后完成 rebase
6. `git diff --check origin/master...HEAD` clean；无冲突标记/secret；所有新增生成物/manifest drift gate clean。
7. Phase 8 完成后把关键 targeted validation **原样连续运行两遍**，两次都通过。
8. 独立 review 完成：P0=0、P1=0；所有 finding 有 disposition；PR 已创建且未 merge。

（完整执行协议见本 GOAL 原始 Prompt：operating envelope、phase budget、autonomy defaults、worktree/commit/rebase/PR runbook、final review instructions——以会话原始 Prompt 为准，本文件为档存副本。）
