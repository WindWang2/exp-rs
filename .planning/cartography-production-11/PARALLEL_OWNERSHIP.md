# PARALLEL_OWNERSHIP — cartography-production-11

启动时（2026-09-16，master=a5b11b7f）open PR / branch 与本 track 的文件级交集与策略。

## 规则回顾（GOAL Phase 0 规则 1–5）

1. 仍开放 PR 的 changed files 默认 read-only，不复制/重做其功能；
2. 依赖其新 API 时优先消费 master 已有稳定 seam；否则改 adapter/contract/test scaffold + follow-up；
3. PR 已合并 → 以新 origin/master 重审计（#991/#992 已按此办理）；
4. 新出现并发 PR 同样适用（每次 rebase 时重跑 `gh pr list`）；
5. open issue 逐条 dedupe（见 BASELINE.md，全部 OUT_OF_SCOPE）。

## Open PR #1009 — execution-runtime-convergence-11（MERGEABLE）

域：src/runtime/**（chunk resume/telemetry/governor/lease）、src/operators/framework、src/processing/framework、src/workflow/pipeline_run_coordinator.cpp、tests/test_*execution*。

| 文件 | 本 track 是否触碰 | 交集处置 |
|---|---|---|
| src/runtime/**、src/operators/framework/**、src/processing/framework/** | 否 | 无交集 |
| src/workflow/pipeline_run_coordinator.cpp | 否 | 无交集（fcntl.h 已在 master） |
| **src/agent/data_platform_tools.cpp** | **是（1 行 build-unblock：`using namespace sicnu::experiment;`）** | 与 #1009 同款修复；rebase 时若冲突，取其版本（语义相同）并在 EVIDENCE 记录 |
| **tests/CMakeLists.txt** | **是（append-only：新测试目标注册）** | append 到 test_mapspec 区块附近；#1009 在其它区块 append → 冲突概率低，rebase 解决 |
| **CHANGELOG.md** | **是（append 一节）** | 双方均 append 头部 → rebase 机械解决 |
| **.gitignore** | **是（append 3 行白名单）** | 同上 |
| data/help/diagnostics.json | 视 Phase 4 需求（若新增错误/诊断码） | 尽量不触碰；如必须，append-only 并记 dedupe |

**读约束**：#1009 引入的 runtime API（ExecutionGovernor/telemetry 等）本 track **不依赖**（cartography 的执行底座是 TaskCenter，已稳定在 master）。

## Open PR #1008 — radiometric-spectral-workbench（CONFLICTING，先需 rebase 的是它）

域：spectral/radiometric（src/core、src/analysis、src/app/widgets/spectral_*、src/agent/spatial_tools/spectral_*）。

| 文件 | 本 track 是否触碰 | 交集处置 |
|---|---|---|
| src/agent/spatial_tools/**、src/core/**、src/analysis/**、src/processing/algorithms/** | 否 | 无交集 |
| **src/agent/CMakeLists.txt** | **是（cartography 区块 append 新源文件）** | #1008 在 spectral 区块 append → 不同区块，冲突概率低 |
| **src/app/CMakeLists.txt** | **目标：完全不触碰**（GUI 改动限于已入构建的 cartography_dock.cpp） | 若被迫触碰则 append-only 并记录 |
| **tests/CMakeLists.txt** | 同 #1009 行 | 同上 |
| .gitignore | 同 #1009 行 | 同上 |

**读约束**：#1008 的 spectral API 本 track 零依赖。

## 本地并行 worktrack

- `zcode/geoai-promptable-foundation-platform-11`（#1009 PR body 披露）：只写 `app/lib/modelops/**`（Python）→ 零交集。

## 本 track 主张的独占写域（审计后确认无并发冲突）

- `src/agent/cartography/**`、`src/agent/mapspec/**`、`src/app/cartography/**`、`data/cartography/**`、`docs/cartography/**`、`tests/test_cartography_*`/`test_mapspec*` —— 全部 open PR/branch 零触碰（#1009/#1008 changed-files 已核对）。

## 共享集成文件改动预算（append-only / minimal diff）

| 文件 | 改动 |
|---|---|
| .gitignore | +3 行白名单 |
| CHANGELOG.md | +1 节 |
| tests/CMakeLists.txt | +新测试源注册（数行） |
| src/agent/CMakeLists.txt | +新源文件注册（数行） |
| src/agent/data_platform_tools.cpp | +1 行 build-unblock（dedupe #1009） |
| data/help/diagnostics.json | 仅当新增诊断码时 append（尽量为 0） |
