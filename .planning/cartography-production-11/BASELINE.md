# BASELINE — cartography-production-11

记录时间：2026-09-16（Phase 0 启动审计；全部为只读观测）

## Prompt 快照 vs 启动时事实（差异即裁决依据）

| 项 | Prompt 生成时（2026-09-15） | 启动时事实（2026-09-16） | 裁决 |
|---|---|---|---|
| origin/master | `ebcafb4d` | **`a5b11b7f10fa010c1c060864fb427d777ba9a4aa`** | 以 a5b11b7f 为基线 |
| PR #991 (grok/unified-mission-workbench-d18) | open | **已合并**（master `c5d4aafe`） | 按规则 3 以新 master 重审计 |
| PR #992 (grok/dataset-foundry-benchmark-d19) | open | **已合并**（master `1cea9892`） | 按规则 3 以新 master 重审计 |
| open PR | #991 #992 | **#1009 (execution-runtime-convergence-11, MERGEABLE)、#1008 (radiometric-spectral-workbench, CONFLICTING/DIRTY)** | 见 PARALLEL_OWNERSHIP.md |
| open issues | 无 | **#1001–#1007**（io/workflow/dataset/georef/agent 域 R2 残留） | 逐条 dedupe：均不在本 track scope |

## origin/master 最近 20 commit（原始）

```
a5b11b7f (origin/master, origin/HEAD) fix: fail-closed fixes for review issues #994–#999 (#1000)
1cea9892 Merge branch 'grok/dataset-foundry-benchmark-d19' (#992)
c5d4aafe D18: Unified Mission Workbench — MissionContext + D14/D15/D17 mounts (#991)
77e178ac fix(ci): resolve macOS/Windows compile errors in d17 and Win32 paths (#993)
08264801 docs(d19): record M6 hermetic scale/evolution/LeaveOne evidence
e5753438 test(d19): hermetic 100k catalog scale, version evolution, leakage Fail, LeaveOne*
753d80e4 docs(d19): record persistence, agent tools, and honest not-executed cmake
1ae47454 feat(agent): D19 foundry/benchmark tool wrappers and hermetic E2E
343ab33d feat(experiment): persist BenchmarkService via ExperimentStore
344f26be feat(experiment): D19 benchmark definition, headless runner, and pins
44617ffa feat(dataset): D19 foundry — roles, FeatureSet join, catalog, QA, service
ec1a4d96 docs(d19): baseline audit, architecture map, and decisions
d3387fcb chore(d19): seed dataset foundry & benchmark planning track
ebcafb4d fix(ci): correct OSR WKT import in test_io_operators (#990)
b91753ff Merge branch 'zcode/classification-change-studio' (#989)
f368b9fd Merge branch 'zcode/workflow-pipeline-designer' (#988)
64418b72 Merge branch 'zcode/geometric-registration-workbench' (#987)
e8c4bf43 feat(temporal): D16 Temporal Phenology Timeline Studio (#986)
461464f2 docs(d15/classification-change-studio): PR body
97cb8485 docs(d14): record full keep-going build outcome (failures pre-existing on master)
```

remote branches（按新近度）：`origin/zcode/execution-runtime-convergence-11`、`origin/zcode/radiometric-spectral-workbench`、`origin/master`。

## Open issues #1001–#1007 dedupe（逐条）

| Issue | 域 | 与本 track 关系 |
|---|---|---|
| #1001 io:clip srcCrsOverride 误作 targetCrs | io | 无交集 → OUT_OF_SCOPE |
| #1002 makeRegistryNodeExecutor artifact 不验证存在（fail-open） | workflow | 无交集 → OUT_OF_SCOPE |
| #1003 joinFeaturesBySampleId JSON-null 列 | dataset | 无交集 → OUT_OF_SCOPE |
| #1004 dataset:qa scan_capped 身份误 Pass | agent(dataset tools) | 无交集 → OUT_OF_SCOPE |
| #1005 mapPickToLayerCrs 吞 transform 异常 | georef | 无交集 → OUT_OF_SCOPE |
| #1006 PipelineRunCoordinator syntheticExecute 软默认 | workflow | 无交集 → OUT_OF_SCOPE |
| #1007 dataset:qa 不审计 CRS 空 schema | dataset | 无交集 → OUT_OF_SCOPE |

## 本地 checkout 说明

- 主仓库工作区在 `007e70cf`（落后 origin/master），**cartography 业务文件在 007e70cf..a5b11b7f 间零变化**（`git diff --name-only` 仅显示 src/agent/CMakeLists.txt +4、tests/CMakeLists.txt +509 及 D18/D19 新文件）→ 对 cartography 现状的代码审计在主仓库执行有效；本 track 全部实现与构建在 worktree（=a5b11b7f）进行。

## master 已知构建事实（与 #1009 PR body 交叉核验）

- `src/workflow/pipeline_run_coordinator.cpp`：**`#include <fcntl.h>` 已在 a5b11b7f 存在（:25）** → #1009 所述该断点在基线已不成立（或其 diff 视角不同）。
- `src/agent/data_platform_tools.cpp`：`BenchmarkService`（`sicnu::experiment`，`src/experiment/benchmark_service.h:23`）在 `namespace sicnu::agent` 内**无限定使用且无 `using namespace sicnu::experiment;`**（:1184/:1221/:1265）→ sicnu_agent 编译会失败；#1009 以 using-directive 修复（同文件）。本 track 需要构建 sicnu_agent → 将做**同款 1 行 build-unblock**并记 dedupe（见 DECISIONS D-007）。
- `tests/test_large_scale_execution_10.cpp:280`：POSIX `unsetenv` 无 MSVC 守护 → 仅影响该测试目标；本 track 不构建/不修改该目标。

## cartography 现状审计结论（subagent #1 只读审计，67 次工具调用，2026-09-16）

引擎唯一、四路 surface（agent tools / RSOperator / CLI pipeline / GUI dock）同引擎；核心缺口 Top10：

1. GUI dock 全同步（`cartography_dock.cpp:265` 直接 `op->run()` 于 GUI 线程；无 TaskCenter/进度/取消）
2. atlas 逐要素导出**零实现**（export.cpp 无 beginRender/feature 迭代；atlas-guide.md 步骤 5 断言无代码支撑）
3. exportMapLayout 内部无进度/取消（dpi 1200/A0 不可中断）
4. 无导出元数据/持久 provenance（structural_digest 不落盘、无 manifest sidecar）
5. 模板无版本迁移机制（对照 upgradeMapSpec mapspec.cpp:1155）
6. 多页/atlas GUI 不可见（preview 仅 page 0，cartography_dock.cpp:446）
7. 确定性导出 best-effort（PDF/SVG 仅设 dpi；无字体嵌入/矢量输出/色彩空间；字体替换仅告警）
8. 无 CLI 专用制图命令（仅 operator 注册进 pipeline）
9. repair 收敛 per-call 无跨调用台账/预算化生产作业
10. atlas 校验浅（仅 enabled⇒coverage_layer + sort key）

已有能力（不得重复造轮子）：bounded solver（hard+soft、kMaxRelaxationPasses=24、决策台账 256）、~40 preflight 规则、repairMapSpecWithLedger（1..10 钳制）、MapSpec v0→v5 升级、条件语法（有界）、组件/模板/解决方案三 registry（extends 环检测、facet、diffTemplates、provenance 戳）、design tokens（screen/print）、确定性排版（CJK wrap/kinsoku/truncation）、inline 图表族（QPainter，matrix≤24×24、≤256 点、表≤64 行）、原子导出（tmp→校验→sha256→rename）+ 既有 E2E（compose→export png + sha256，test_cartography_operators_10.cpp:125-183）。

## Open PR changed files 摘要（原始）

- **#1009** (MERGEABLE, base master): `.gitignore .goal-loop-ledger.md .planning/execution-runtime-convergence-11/** CHANGELOG.md data/help/diagnostics.json docs/execution/ADOPTION_GUIDE.md docs/execution/CURRENT_ARCHITECTURE.md src/agent/data_platform_tools.cpp src/operators/CMakeLists.txt src/operators/framework/** src/processing/framework/{fused_chain.cpp,local_worker_pool.*} src/runtime/** src/workflow/pipeline_run_coordinator.cpp tests/CMakeLists.txt tests/test_{chunk_*,execution_*,worker_lease_11,chunk_graph,fused_chain,large_scale_execution_10,external_memory_10,job_engine,worker_host}*`
- **#1008** (CONFLICTING, base master): `.gitignore .planning/radiometric-spectral-workbench/** docs/adr/0158* src/agent/CMakeLists.txt src/agent/spatial_tools/{spatial_tool.cpp,spectral_spatial_tools.*} src/analysis/{atmospheric,hyperspectral}/** src/app/CMakeLists.txt src/app/widgets/{band_composite_palette,spectral_profile_widget}.* src/core/{radiometric_state,spectral_library}.* src/processing/algorithms/{radiometric_calibration,spectral_indices,spectral_unmixing}.* tests/CMakeLists.txt tests/test_{continuum_removal,d13_radiometric_spectral_e2e,fast_6s_atmospheric,radiometric_calibration,radiometric_state,spectral_*}.cpp`
