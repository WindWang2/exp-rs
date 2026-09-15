# BASELINE — Phase 0 只读审计原始记录（2026-09-16）

所有命令在主仓库 `C:\Users\wangj.KEVIN\projects\exp-rs` 执行；worktree 创建于审计完成之后。

## origin/master 刷新

```
git fetch origin --prune
git rev-parse origin/master
→ a5b11b7f10fa010c1c060864fb427d777ba9a4aa
```

**Prompt 快照已失效**：快照称 origin/master=`ebcafb4d`、#991/#992 open。实际启动时：

```
git log -12 --oneline origin/master
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
```

→ **#991 (D18) 与 #992 (D19) 均已合并进 master**。按 GOAL 规则 3：以新 origin/master 为事实源，不再把它们当 open-PR 排他区；其代码成为可依赖的 master seam（但本 track 仍不重写 IR2/dock/foundry 功能，E 包遵守"不修改 D18 IR2 文件"的原意——以 harness 侧投影实现）。

## Open PRs（启动时）

### #1009 `zcode/execution-runtime-convergence-11`（execution-11 runtime convergence）
- mergeStateStatus=UNSTABLE（无碍本 track；不等待其 CI）。
- changed files（`gh pr diff 1009 --name-only`，关键项）：
  - `src/runtime/chunk/**`（chunk_pipeline, disk_tile_store, memory_planner, resumable_tile_run, tile_checkpoint, tile_run_contract）
  - `src/runtime/exec/execution_governor.*`、`src/runtime/observability/execution_telemetry.*`、`src/runtime/worker/worker_lease.*`
  - `src/operators/framework/{chunk_error_bridge.h, chunked_run.*, rs_operator_context.h, rs_operator_error.*}`
  - `src/processing/framework/{fused_chain.cpp, local_worker_pool.*}`
  - `src/workflow/pipeline_run_coordinator.cpp`
  - `src/agent/data_platform_tools.cpp`
  - `tests/CMakeLists.txt` + 9 个新 test 文件
  - 共享：`.gitignore`、`CHANGELOG.md`、`data/help/diagnostics.json`、`src/operators/CMakeLists.txt`、`src/runtime/CMakeLists.txt`、`tests/CMakeLists.txt`
- **与本 track primary scope 交集 ≈ 0**（它不触 `src/agent/harness/**`；`pipeline_run_coordinator.cpp` ≠ `workflow_run_coordinator.cpp`）。
- 策略：`src/workflow/pipeline_run_coordinator.cpp` 视为 #1009 只读区；`tests/CMakeLists.txt`/`CHANGELOG.md`/`src/agent/CMakeLists.txt` 等共享注册文件只做 append-only 最小接线，并在 PR_BODY 声明。

### #1008 `zcode/radiometric-spectral-workbench`（spectral D13）
- mergeStateStatus=DIRTY（CONFLICTING）。
- changed files 关键项：`src/core/{radiometric_state.*, spectral_library.*}`、`src/processing/algorithms/{radiometric_calibration.*, spectral_indices.*, spectral_unmixing.*}`、`src/analysis/{atmospheric/fast_6s_lookup.*, hyperspectral/continuum_removal.*}`、`src/agent/spatial_tools/spectral_spatial_tools.*`、`src/agent/CMakeLists.txt`、`src/app/widgets/{band_composite_palette.*, spectral_profile_widget.*}` + 各自 tests。
- **交集**：`src/agent/CMakeLists.txt`（共享注册）与 `src/core/radiometric_state.*`（本 track 只读消费其 calibration 语义，不修改）。
- 策略：不动 #1008 的任何业务文件；radiometric 数值域语义复用 master 已有 `artifact_facts` 数值域词汇与 `facts::radiometricState`。

## Open issues（启动时全部 7 条；逐条 dedupe）

| # | 标题（缩略） | 归属 | 与本 track 关系 |
|---|---|---|---|
| 1001 | io:clip 用 srcCrsOverride 当 targetCrs（silent wrong clip） | io 算子 | 范围外（operators），记 OUT_OF_SCOPE 线索 |
| 1002 | makeRegistryNodeExecutor 不验证 artifact 文件存在（fail-open） | workflow 引擎 | 范围外（#1009 已改 pipeline_run_coordinator，避让） |
| 1003 | joinFeaturesBySampleId 把 JSON-null 必需列当存在 | dataset foundry | 范围外（D19） |
| 1004 | dataset:qa scan_capped 时 identity 仍 Pass（#996 残留） | dataset foundry | 范围外（D19）；但"cap 必须 fail/降级诚实"原则与本 track C 包 UNKNOWN 纪律一致，仅作设计参照 |
| 1005 | mapPickToLayerCrs 异常时返回未变换画布点 | georef UI | 范围外 |
| 1006 | PipelineRunCoordinator 仍 soft-default syntheticExecute | workflow 引擎 | 范围外（#1009 正在改该文件） |
| 1007 | dataset:qa 从不审计 CRS（空 schema.crs 混合/未指定） | dataset foundry | 范围外（D19） |

**结论：无 open issue 落在本 track primary scope 内需要本 track 修复**；全部记为线索/OUT_OF_SCOPE。

## ISSUES.md（旧 D3 backlog）核验

内容为 D3 教学内容 track 的算子缺口登记（T-1/T-2/T-3/S-1/S-2/H-1/H-2/H-3/C-1/C-2）。
对照 CHANGELOG：T-1/T-2/T-3/C-2 已由 Temporal Platform 10.0 修复（`rs:temporal_regularize`、`rs:temporal_harmonic_breaks`、`rs:temporal_extract_regions`、monitor scenes seam）。
其余（S-2 极化分解、H-1/H-2/H-3、C-1）为算子缺口，**不属于本 track ownership**（本 track 是编译器/事实/grounding，不是算子内核），记 OUT_OF_SCOPE。

## 代码现状审计（关键事实，含文件:行号）

1. **WorkflowIR 1.0 已在 master**：`src/agent/harness/workflow_ir.h`（249 行）——typed IR、fact staging "observed/declared/derived/assumed/unknown"（workflow_ir.h:20-26）、`irLimits()` 单表、fail-closed reader、`mergeArtifactFacts`/`factStatusFor`、`workflowIrFingerprint`、`normalizedCrsAuthid`。
2. **静态分析 17 检查族**：`src/agent/harness/workflow_analysis.cpp`（1506 行）——check ledger：modality(:933-977)、wavelength(:1042-1065)、temporal(:1071-1112)、SAR calibration(:1126+)、resource_over_budget(:689-698)、output_path_collision(:720-746)、crs/grid(:1301 附近)、categorical、fact_conflict 等；status=pass/fail/warn/skip（:488-501 builder）；UNKNOWN→skip/warn，不伪 PASS。
3. **repair 闭表**：`src/agent/harness/workflow_repair.h`——risk class shape_preserving/radiometric/science_changing（:44-48）；`planRepairs` 纯函数；`analyzeRepairAnalyze`。radiometric/science_changing 永不自动插入 → decision_required refusal。
4. **staged planner**：`workflow_planner.cpp:427,456`（analyze→repair→re-analyze）；surface `harness:compile_workflow`（:556, name :309）。
5. **session checkpoint**：`context_checkpoint.h`——stage cursor、fact identity stale invalidation（path/size/mtime/revision）、`SICNU_HARNESS_SESSION_DIR`。
6. **grounding**：`grounding_tools.cpp`（535 行）——`spatial:understand` 从 raster/vector inspect 组装 understanding + `fact_status` + modality 推断；缓存 key=(path,revision)（:144,233-246）；`harness:context`。`cachedUnderstandingFor` explain-only。
7. **band_facts**：`band_facts.h/.cpp`（172 行）——roles/wavelengths/grid/radiometric/SAR facts/`acquisitionTime`（单一时间字符串，:162-170）。
8. **evidence sidecars**：`evidence.h`——provenance/uncertainty/verification；QSaveFile atomic；"harness never fabricates uncertainty"。
9. **lowering + metadata seam**：`agent_plan.cpp:301-369` `compilePlanToWorkflowJson`——metadata 根键现有 {plan_fingerprint, intent, cleanup, pins}；注释明言 "execution-plane consumption is a recorded cross-track follow-up"（:362-364）→ **本 track E 包的正式 seam**。
10. **execution plane**：`src/workflow/`（workflow_definition/workflow_run/workflow_checkpoint/workflow_run_coordinator…）；`workflowDefinitionFromJson`（definition.h:14）解析；derivation 记录在 `data/derivation_record.h`；`tests/test_workflow_resume_provenance.cpp` 是 provenance E2E 范本。
11. **Pi bridge**：`pi/`（exp-rs-spatial.ts, mcp_bridge.ts, knowledge/, roles/, test/{bridge_parity,no_drift,mcp_bridge}.test.mjs）——ADR 0149 decision 9 的 single-implementation + drift guard 已在。
12. **eval corpus**：`tests/test_harness_eval_corpus.cpp`——data-driven（`data/agent/evals/cases`），≤400 cases、categories 闭表（:52-62 含 anti_hallucination/typed_contract/prompt_injection 等）、runtime-generated GDAL fixtures（≤32px、≤8 bands）。
13. **错误 taxonomy**：`harness_error.h` 单表；compiler 七码已 additive（:65-71）。
14. **presets**：`CMakePresets.json` configurePresets = dev-default/ci-fast/ci-full/sanitizer-debug/release-package。**GOAL 所写 `build-dev` 不存在，实际 preset 为 `dev-default`**（记 DECISIONS）。
15. **understanding 允许事实键**（workflow_analysis.cpp:286-288 闭表注释）：crs, crs_authid, size, pixel_size, extent, band_roles, band_count, bands, wavelengths_nm, modality, sensor, dtype, nodata, quality_masks, polarizations, …（temporal_facts/temporal 由 temporal 检查读取，analysis :1074-1077）。

## 真实缺口（11.0 的可验证 gap，与 mission 对齐）

- **G1（A 包）**：时间事实只有单景 `acquisition_time` 字符串；无 cadence/regularity/date list/coverage 区间、无时区/日历规范化。understanding 无 product generation facts（产品级/处理级 L1/L2 等）、quality_masks 无规范化语义、model task facts 无统一投影、resource facts 只有 plan 级估算（agent_plan.cpp:415 estimatePlanResources）无 per-node 来源等级。
- **G2（B 包）**：grounding 只有全量 `spatial:understand`；无 fact-scoped bounded probe（按需字段、超时、字节上限）、无 probe 失败的 typed 语义、无 model manifest/product registry 的 bounded 探针 seam。
- **G3（C 包）**：temporal 检查仅对 capability demand 做 min_scenes/requires_time 类断言；无跨节点日历对齐（多输入时间覆盖矛盾）、数值域链式传播检查（dn→reflectance 混链）、输出语义 identity 检查（declared kind vs 上游事实）。
- **G4（D 包）**：repair 有 risk class 但无 cost/evidence 排序的显式 decision 文档（prepared decision 未参数化到"可直接执行的 wiring+params"）。
- **G5（E 包）**：metadata seam 已留但 `compiler` 块（ir fingerprint/facts/repairs/refusals/checks）尚未投影；无 compile 级 sidecar 与 run 的关联。
- **G6（F 包）**：`lab_diagnostics`/diagnose_run 有 run 级 ledger，但无"从失败回到导致决策的那条事实/契约"的 trace（zh-CN bounded）。
- **G7（G 包）**：eval corpus 无 compiler/grounding 专属 case 族（temporal cadence、mixed modality chain、refusal→prepared decision）。
- **G8（H 包）**：bridge 有 no_drift/parity，但无 knowledge-budget/desync/abort/schema-drift 对 compiler surface 的专门守护。
