# Integration — Scientific Data Passport ↔ other tracks

本文件是 RS14-01 科学状态层与并发方向 / 未来消费者的**唯一接线清单**。
所有接线点均为建议的最小 delta,不要求对方 track 依赖本方向内部类型。

## 1. Agent/MCP 只读工具(推迟的接线点)

现状:`passport` 通过 CLI(`sicnu_geo_rs_cli passport --path <file> --json`)与库 API
(`resolveAssetState`)提供 machine-readable 查询。MCP 进程内工具**有意推迟**:
注册一个 data tool 需要同时改 `src/agent/mcp_server.cpp`(tools/call 分发)+
`mcp_server.h`(handler 声明)+ 一个 `ToolProvider` 定义——前两者是 20 个并发
track 的高冲突中央文件,而 capability 门禁(completeness/drift)只覆盖 `rs:`
算子,不含 data 工具,风险收益不成比例。

未来接线(对齐 `data:get_lineage` 的既有模式):
1. `src/agent/tool_catalog/data_tool_provider.cpp` 增加 `makeAssetPassportTool()`
   (input schema:`{"asset_id": string}`;family `data`;name `data:asset_passport`)。
2. `mcp_server.cpp` tools/call 分发增加 `data:asset_passport` 分支,handler:
   - `DataManager::asset(id)` → `makeCatalogFacts(snapshot)`;
   - snapshot 的 `source().canonicalSource` → `collectDatasetFacts(path)`;
   - `resolveAssetState(input)` → `assetStateToJson` → bounded QVariantMap。
3. surface parity 自动成立:catalog provider 的工具经 UNION 投影进入
   MCP tools/list、CLI tools、get_tool_schema 三个表面。

## 2. 教学 UI 面板(GUI 接线点)

`renderTeachingSummary(state)` 是 Qt-free 视图模型;面板侧只需要:
- 参照 `src/app/panels/asset_catalog_index.{h,cpp}` 的资产选中信号;
- 对选中资产调用 CLI 等价的链路(collect→resolve);
- 用 `RsResultSummary`(`src/app/widgets/rs_result_summary.h`)的
  `setResult(Json::Value)` 模式渲染五个证据桶;或直接渲染
  `teachingSummaryToPlainText` 的文本。
不新增业务逻辑——所有科学语义都留在 core。

## 3. RS14-09 Task Planner

Planner 在生成 ScientificPlan 前可用护照做前置条件检查:
- 读 `claims[path].kind`:仅当 `known` 才可作为硬前置;`assumed` 需要用户确认;
  `unknown` 进 plan 的开放问题;`conflicted` 必须显式阻塞。
- 库 API 即接口:`resolveAssetState` + `claimFor`。

## 4. RS14-10 Unified Scientific Verifier

验证器可以把护照作为**输入证据**而不是重新采集:
- `sicnu.asset_state.v1` 是确定性 JSON,可进入 verifier 的证据集;
- `radiometric.unit` + `domain` + `numeric_scale` 支持"两幅影像辐射可比"类检查;
- 不要求 verifier 依赖 core 库——直接消费 JSON 即可。

## 5. RS14-17 Reproducibility Capsule

护照可导出进 capsule(只读工件):
- `serializeState` 字节确定 ⇒ capsule 内 diff 稳定;
- schema id 已含版本,迁移策略与 capsule 的 versioning 对齐。

## 6. RS14-08 Capability State Graph

无共享类型。能力图以 operator 为键,护照以 asset 为键;两者在
"agent 决策前检查"场景相遇(能力图回答"能否做",护照回答"数据现在是什么")。

## 7. 与现有事实源的关系(单一事实源声明)

| 事实 | 权威 | 护照角色 |
|---|---|---|
| 资产身份/结构 | `CatalogRecordStore` / `AssetSnapshot` | 投影(source tag `catalog:*`) |
| 文件元数据 | GDAL `SICNU_*` 键 | 投影(source tag `gdal:*`) |
| 传感器波段轴 | `data/products/sensor_profiles/*.json` | 投影(inferred claims;本 track 定义了 `SensorProfileFacts` 接缝与解析器语义,真实注册表加载器的适配器是留给接线的休眠接缝,见 integration 顶部模式) |
| 谱系 | `DerivationRecord` | 投影(`provenance` section) |
| 模型 sidecar | `<model>.meta.json` | 投影(`model_derived` section) |

护照不缓存、不写回、不替代上述任何来源;解析永远按需进行,复杂度
O(bands + facts + claims)(confidence 经 path→kind 索引,无 bands×claims 项)。
# Integration Notes — RS14-06 Experiment Debugger

This track ships a self-contained read-only library plus docs. It deliberately does NOT wire
into CLI/GUI/MCP to keep the central-file delta minimal for the RS14 union merge. The seams are
stable value objects; wiring is mechanical.

## What exists now

- `src/experiment/debugger/` — `sicnu_experiment_debugger` (alias `Sicnu::experiment_debugger`),
  links `Qt6::Core + Sicnu::dataset + Sicnu::experiment + sicnu_workflow` (same combination as
  `sicnu_experiment_bridge`). Layer guard: no GUI, no network.
- `tests/test_experiment_debugger.cpp` — per-slice contract suites.
- `docs/adr/0174-experiment-debugger-14.md`, `docs/experiments/debugging.md`.

## Wiring points (future tracks; no code exists here for these)

### CLI (`src/cli/cli_dataset_commands.cpp`)
Sketch: `experiment debug --reference <runId> --student <runId> [--dir <runDirectory>]
[--profile <profile.json>]` —
1. `ExperimentStore store; store.open(dbPath)` read-only;
2. `DirectoryEvidenceSource source(&store, runDir);`
3. `RunSnapshotBuilder b(source); auto r = b.build(ref); auto s = b.build(stu);`
4. `FirstDivergenceAnalyzer::analyze(refRun, stuRun, refSnap, stuSnap)` — signature order
   `(referenceRun, studentRun, referenceSnapshot, studentSnapshot)`; the profile overload appends
   `const EquivalenceProfile&` before the options;
5. print `report.toJson()` (already human-readable), exit nonzero when `verdict == "divergent"`.

### GUI (workbench `dataset_experiment_panel`)
The panel's existing `compareSelectedRuns()` opens the whole-run `RunComparison`. Add a
"first divergence" action: build the two snapshots via the panel's store + run directory and
render `TimelineDiffModel::build(...)` — a pure value model; a list/tree widget suffices. The
model is Qt-free by design; the GUI owns presentation only.

### MCP / agent surface
`AgentDiagnosticAdapter::forReplan(report)` returns the `exp.diag.v1`-aligned object
(`experiment.debugger.*` codes). A `debug_run` tool would wrap: inputs
`reference_run_id, student_run_id, profile?`, output = `AgentDiagnostic.toJson()` +
`FirstDivergenceReport.toJson()` as the tool's structured payload. No new taxonomy: codes ride
the existing diagnostic vocabulary and the `diagnostic_catalog` can index them via
`data/help/diagnostics.json` entries (additive).

### RS14-09 Scientific Task Planner (PR #1193)
Consume `AgentDiagnostic` as a DTO after a failed/rejected plan execution: `code` +
`details.divergence_kind` + `details.confidence` are the replan signal; `suggested_action` is
advisory text. No type dependency — copy the struct or parse the JSON.

### RS14-10 Unified Scientific Verifier (PR #1191)
A `FirstDivergenceReport` is admissible EXPLANATION evidence for a verifier's reject verdict
(run-level `non_comparable` / `divergent` verdicts already distinguish "wrong data" from "wrong
process"). The verifier may read reports produced elsewhere; the debugger never calls the
verifier.

### RS14-17 Reproducibility Capsule (PR #1188)
A capsule that wants a run-content stamp should embed the canonical
`RunSnapshot::identityDocument()` (or its hash) — `snapshotDigest()` additionally covers the
run id, so two records of the same execution compare equal via the identity document, not the
per-run digest. Both are versioned (`exp.debugger.snapshot.v1`); capsule-side validation
compares these, it does not recompute evidence.

## Evidence directory convention

`DirectoryEvidenceSource(store, runDirectory)` expects the platform's recorded layout:
`checkpoint_<runId>.json` and/or `provenance_<runId>.json` in the run directory root or in
`attempt-<N>/` subdirectories (highest attempt preferred). This matches
`PipelineRunCoordinator`/`WorkflowCheckpointManager` writers. The store is optional (pass
nullptr for evidence-directory-only use).

## Versioning

All emitted documents are schema-gated: `exp.debugger.snapshot.v1`,
`exp.debugger.divergence.v1`, `exp.debugger.equivalence.v1`, `exp.debugger.invariants.v1`
(`schema_version: 1`). Unknown kinds/versions are refused, never reinterpreted.
# Integration seams — RS14 agent-benchmark ↔ platform

This track ships a self-contained pure-C++ harness. The points below are the
DESIGNED wiring surfaces for future tracks; none of them are required for the
harness to work, and none were built here to avoid cross-track coupling.

## 1. Live trace capture (agent run-loop / harness session)

`AgentTrace` (`sicnu.agentbench.trace/v1`) is the target projection for a
session recorder. A future track can serialize real agent sessions (run-loop
steps, SpatialToolResults, token counts) into traces. Injected faults must be
marked `payload.fault = <kind>` for `recovery_quality` to observe them;
without markers the metric is honestly `null`
(`faults_not_observable_in_trace`).

## 2. Persisting suite reports (ExperimentStore)

Suite/case reports are files, not a new store. The single-store discipline
stays intact: a Qt-side adapter can project a suite report into
`ExperimentStore::saveBenchmarkResult` (D19 tables, additive, schema stays
v1) keyed by `suite_id@version` + `pack_digest`. Nothing in `src/agentbench`
links Qt — the adapter belongs to the experiment track.

## 3. MCP surface

Extend the existing `benchmark:` namespace in `src/agent/data_platform_tools.cpp`
(e.g. `benchmark:agent_suite_run`, `benchmark:agent_report_read`) instead of
adding a new namespace. The harness's pure functions are directly callable
from that lane.

## 4. Teaching / lab surface

Per-case Markdown reports are designed for classroom review: verdict,
per-metric table (including *why* a metric is unavailable), hidden-invariant
outcomes, and resource usage. The lab track can render `renderReportMarkdown`
output next to lab reports (`sicnu.labreport.v1` stays the lab truth source).

## 5. Fake agent as oracle harness

`runScript` gives deterministic agent behavior for any surface that needs a
repeatable "agent" (determinism censuses, UI demos, grading examples). It is
NOT a planning engine — scripts are explicit policies, not intelligence.

## Boundary rules honored by this track

- No second registry, store, or provenance system.
- No metric-formula duplication (agent-behavior metrics only).
- No engine execution; no network; no new CI.
- Open issues in the avoid-list were not touched (see
  `.planning/RS14-14-agent-benchmark/recon.md` dedup matrix).

---

# Integration seams — RS14-07 Parameter Sensitivity & Uncertainty Studio ↔ platform

The study layer (`src/study`, `Sicnu::study`) is complete and offline: specs,
samplers, runner, analysis and the versioned report
(`sicnu.studyspec.v1` / `sicnu.studyreport.v1`, see
[experiments/parameter-studio.md](experiments/parameter-studio.md)). The
production execution adapter (`src/study/bridge`, `Sicnu::study_bridge`) is
wired into the build and consumed by the full-stack e2e test. The surfaces
below are the DESIGNED wiring points for future tracks; none are built here.

## 1. Agent / MCP surface (`study:*` tools)

`StudyService`-style wiring belongs in the existing agent tool lane
(`src/agent/data_platform_tools.cpp`, `surface_registry.cpp`), registering
e.g. `study:spec_validate`, `study:run`, `study:report_read`. The reader side
is already safe for that: `ParameterStudySpec::fromJson` refuses unknown
fields/versions with typed `study.*` codes, and the report parses standalone.
Nothing in `sicnu_study` links the agent stack, so the tools can call it
without new cycles. Deliberately NOT registered here to avoid colliding with
agent-surface tracks and the capability-mirror regeneration (#1151 stays
someone else's fix).

## 2. Teaching UI (workbench panel)

`StudyRunRow` (`study_export.h`) is the DTO a `QAbstractTableModel` panel
binds — thin-client pattern over the report projection (cf.
`dataset_experiment_panel`, `processing_history_model` row-cap + truthful
truncation). The panel renders rows/curves; it never derives business facts:
status accounting and trend labels come from the report, not from widget
logic. Sample wiring point: a study panel beside
`dataset_experiment_panel`, loading `sicnu.studyreport.v1` documents via the
tolerant report reader.

## 3. Execution plane consumers

`ExecutionPlaneStudyBackend` is opt-in: only surfaces that link
`Sicnu::study_bridge` gain study execution. The bridge's commit policy
(staged temp → stable rename, no catalog asset) and its ordering assumption
(the study is the only payload builder for its task ids; foreign builders
turn into typed `study.run_output_mismatch` failures) are documented in
`study_execution_plane.h`. Future CLI (`study run --spec …`) should reuse the
bridge rather than re-implementing a backend.

## 4. Reproducibility capsule (RS14-17) and lab runtime

A study report is a projection of `ExperimentStore` truth, so a capsule can
wrap the report + the referenced runs without a new exporter: the report
echoes the full spec (`spec_json`) and per-point parameters, which is the
replay contract (same spec+seed ⇒ same pointIds). LabSpec labs can embed an
exemplar spec (`examples/studies/*.sicnu-study.json`) as a lab step template.

## Boundary rules honored by this track

- No second registry/store/provenance: runs live in `ExperimentStore`, point
  identity IS the matrix cellId, Pareto reuses `MatrixAggregator`.
- No TaskCenter/Workflow/operator changes; the in-flight window is a
  submission bound, not a scheduler.
- Open issues in the avoid-list were not touched (see
  `.planning/RS14-07-parameter-studio/recon.md` dedup matrix).
