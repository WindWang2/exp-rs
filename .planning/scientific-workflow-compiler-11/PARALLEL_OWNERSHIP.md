# PARALLEL_OWNERSHIP — 启动时并发轨道/文件归属（2026-09-16，master=a5b11b7f）

## 原则（GOAL 规则落地）

1. open PR 的 changed files 默认 read-only；
2. 依赖其 API 时优先消费 master 已有稳定 seam，否则改为 adapter/contract/test scaffold + PR_BODY 标 follow-up；
3. PR 已合并 → 以新 origin/master 重审计（#991/#992 已按此办理）；
4. 新出现并发 PR 同样适用；
5. open issue 逐条 dedupe（见 BASELINE.md issues 表）。

## Open PR × 本 track 文件级交集

| Open PR | 业务主战场 | 与本 track primary scope 交集 | 处置 |
|---|---|---|---|
| #1009 execution-runtime-convergence-11 | src/runtime/**, src/operators/framework/**, src/processing/framework/**, src/workflow/pipeline_run_coordinator.cpp, src/agent/data_platform_tools.cpp | 无业务交集（不触 harness/）。共享文件：tests/CMakeLists.txt、CHANGELOG.md、.gitignore、data/help/diagnostics.json | 共享注册文件 append-only 最小接线；**不触碰 `src/workflow/pipeline_run_coordinator.cpp` 与 #1009 的新增 runtime 文件**；E 包投影落在 harness 侧（agent_plan/workflow_planner + 新模块），不进 runtime |
| #1008 radiometric-spectral-workbench | src/core/{radiometric_state,spectral_library}, src/processing/algorithms/{radiometric_calibration,spectral_indices,spectral_unmixing}, src/analysis/{atmospheric,hyperspectral}, src/agent/spatial_tools/spectral_spatial_tools.*, src/app/widgets/{band_composite_palette,spectral_profile_widget} | src/agent/CMakeLists.txt（共享注册） | 不动其业务文件；radiometric 语义只读消费 master 已有 `artifact_facts` 数值域与 `facts::radiometricState`；`src/agent/CMakeLists.txt` 若需登记新文件则 append-only |

## Read-only 区（本 track 承诺）

- `src/workflow/**`：执行平面只读（E 包只经 `compilePlanToWorkflowJson` 的 metadata seam + harness 侧新模块投影）。例外：无 —— 本 track 不写 `src/workflow/**` 任何文件。#1009 正在改的 `pipeline_run_coordinator.cpp` 自然被覆盖。
- `src/runtime/**`、`src/operators/**`、`src/processing/**`、`src/core/**`、`src/analysis/**`、`src/app/**`、`src/cli/**`：全部只读（master 权威或他人 PR 主场）。
- `data/agent/evals/cases/**`：**本 track 可追加**（G 包）—— eval corpus 是数据而非业务代码，历史 track 先例（harness 8/9）均直接追加 case 文件；追加保持 schema 一致并受 ≤400 cases 硬上限与 corpus runner 校验。
- `pi/**`：H 包允许在 `pi/test/` 追加 node 测试与（若需要）`pi/knowledge` 数据；不改 `exp-rs-spatial.ts`/`mcp_bridge.ts` 业务实现除非 drift 修复必须（则记 DECISIONS + 最小 diff）。
- `docs/adr/**`：允许新增一条 ADR（本 track 设计决定），不改他人 ADR。
- `CHANGELOG.md`：append-only 独立 integration commit。
- `tests/CMakeLists.txt`、`src/agent/CMakeLists.txt`、`src/agent/harness/CMakeLists.txt`（如存在）等注册文件：append-only，推迟到 integration commit。

## 本 track 独占写区（primary）

- `src/agent/harness/**`（新增模块 + 既有模块的最小扩展）
- `tests/test_harness_*`、`tests/test_workflow_ir*`、`tests/test_workflow_analysis*`、`tests/test_workflow_repair*` 等本 track 新增/对应测试
- `docs/agents/**`、`docs/adr/01xx-scientific-workflow-compiler-11…`（新文件）
- `.planning/scientific-workflow-compiler-11/**`
