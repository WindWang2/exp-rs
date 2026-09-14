# DECISIONS — workflow-pipeline-designer (D17)

`autonomy=full`: decisions are taken locally and recorded here; the user is
not asked. Each entry: context → decision → consequences.

## D1 — Seam file paths deviate from the brief where master already occupies them

**Context.** The D17 brief names seam headers that, on `origin/master`
@ `007e70cff6`, are already occupied by the ADR 0149 Scientific Workflow
Compiler 10.0 stack:
- `src/agent/harness/workflow_ir.h` — WorkflowIR **1.0**
  (`sicnu::agent::harness`, `std::string`/jsoncpp, tested by
  `test_workflow_ir.cpp`).
- `src/agent/harness/workflow_repair.h` — 1.0 rule-table repair engine.
- `src/app/widgets/guided_workflow_widget.h` — global-namespace
  `::GuidedWorkflowWidget` (LabSpec renderer, integrated into the main window).
- `src/workflow/workflow_run_coordinator.{h,cpp}` — production singleton
  bridging TaskCenter ↔ Workflow Engine 2.0 checkpoints.

Overwriting any of them would break shipped systems (violates Surgical
Changes). The brief itself calls the new document "Workflow IR **2.0**" and
requires `migrateFromV1` — i.e. the existing IR *is* V1, so V2 must coexist.

**Decision.** D17 code is a new Qt-native layer in `namespace sicnu::workflow`
(exactly as the brief specifies the namespace), in non-colliding files:

| Brief seam | Actual seam (D17) | Reason |
|---|---|---|
| `src/agent/harness/workflow_ir.h` | `src/workflow/workflow_ir_v2.{h,cpp}` | 1.0 occupies the original |
| `src/agent/harness/workflow_dag_analyzer.h` | `src/workflow/workflow_dag_analyzer.{h,cpp}` | colocated with the IR it consumes |
| `src/agent/harness/workflow_repair.h` + `src/workflow/contract_checker.h` | `src/workflow/contract_checker.{h,cpp}` + `src/workflow/workflow_repair_engine.{h,cpp}` | 1.0 repair occupies the original; checker name kept |
| `src/workflow/plan_optimizer.h`, `workflow_cost_estimator.h` | as briefed (both were free) | — |
| `src/workflow/workflow_run_coordinator.h` | `src/workflow/pipeline_run_coordinator.{h,cpp}` (`PipelineRunCoordinator`) | the briefed name is the production TaskCenter bridge; D17 needs a self-contained, instance-based coordinator with Qt signals |
| `src/app/pipeline/pipeline_canvas_widget.h` + scene/node/connection/port items | as briefed (dir was empty) | — |
| `src/app/widgets/guided_workflow_widget.h` | `src/app/pipeline/guided_workflow_workbench.{h,cpp}`, class `sicnu::app::workbench::GuidedWorkflowWidget` | original file occupied; C++ namespacing makes the class-name reuse collision-free |
| `src/agent/tools/workflow_orchestrator_tool.h` + `workflow_agent_compiler.h` | as briefed (dir created) | — |

**Consequence.** Class names, namespaces, APIs and semantics follow the brief;
only file paths of five seams move, each documented above.

## D2 — Migration `migrateFromV1` targets the shipped IR 1.0 schema

The V1 input is the *real* ADR 0149 document (`kind: "workflow_ir"`,
`schema_version`/`version: "1.0"`, nodes with typed ports + artifact facts).
The migrator maps node ids, operator ids, port wiring and artifact facts into
`NodeFact`/`EdgeFact`, defaulting missing `radiometricState` from the artifact
numeric domain (`dn` → DN, `surface_reflectance` → BOA, `toa` → TOA, `index` →
Index, `masked` → Mask, else None) and `canvasPosition` (4-per-row grid),
derive distinct default input-port names for as-less wirings, and fails
closed on malformed documents.

## D3 — Execution substrate: dedicated `QThreadPool`, not the JobEngine singleton

**Context.** The brief mentions `sicnu::jobs::JobEngine`. That engine is a
process-global scheduler wired to `RSOperatorRegistry` and shared with
TaskCenter/production traffic.
**Decision.** `PipelineRunCoordinator` owns a private `QThreadPool` sized by
`CostEstimator::recommendedMaxParallelism` (≤ hardware), and executes nodes
through an injectable `NodeExecutor` callback. Rationale: hermetic tests
(`ctest -j1`), bounded memory per the RSS waterline, no coupling to the
operator registry for synthetic D17 operators, and cancellation that cannot
interfere with unrelated production jobs. No worker ever blocks synchronously
on another node (issue #798 discipline): readiness is event-driven via the
Kahn frontier.
**Consequence.** JobEngine remains the production operator host; the D17
engine is the *workflow-level* scheduler above it.

## D4 — Synthetic deterministic node executor for tests/E2E

Labs and scale tests must run green offline and fast. The coordinator accepts
an injected executor map (`operatorId → callable`). D17 ships
`makeSyntheticRasterExecutor()`: any `rs:*`/`lab:*` operator simulates a
deterministic per-pixel transform derived from the node signature and writes
a real artifact file (PPM/binary) into the run directory. Ground truth for
E2E = topology/schedule/checkpoint/determinism properties, not pre-baked
gold TIFFs (repo has no gold raster corpus for these labs; noted in PLAN).

## D5 — SHA-256 via `QCryptographicHash`, canonical JSON via sorted QJsonObject keys

Qt ships Sha256; jsoncpp is not needed in the V2 layer (QJson only). Canonical
form: keys sorted (QJsonObject iterates sorted), numbers via
`QJsonValue::toDouble()` shortest round-trip, UTF-8, no whitespace. Parent
signatures are concatenated in lexicographic node-id order per the brief's
`⊕^Sorted` clause.

## D6 — Repair rule table is closed and ordered

`{CrsMismatch → rs:reproject(bilinear)}`,
`{ResolutionMismatch → rs:resample(bilinear)}`,
`{RadiometricStateMismatch DN→Radiance → rs:radiometric_calibration`,
 `DN→TOA|BOA → rs:radiometric_calibration then rs:atmospheric_correction`,
 `Radiance→TOA|BOA → rs:atmospheric_correction}`,
`{DataTypeMismatch → rs:convert_dtype}`. DimensionMismatch is enumerated but
not derivable from today's PortFact facts, so no rule fires for it (checked
and documented, not silently invented). Application order = violation
enumeration order (deterministic source-order); inserted node ids are
`adapter_<edgeId>_<operator>` with numeric collision suffixes. The repair
invariant (post-inspect empty) is a test gate.

## D7 — Checkpoint atomicity = QSaveFile-equivalent tmp+fsync+rename, done manually

`checkpoint_<run_id>.json.tmp` → write → `flush` → `::fsync` (POSIX) /
`_commit` (Win32) → `rename`. Qt's `QSaveFile` performs tmp+rename but not
fsync-before-rename on all platforms; the D17 writer does all four steps
explicitly (matches the #960 offline-atomic pattern). Resume: load JSON,
`CacheHit` every node whose recorded signature matches the recomputed lineage
signature **and** whose artifact file still exists — otherwise recompute.

## D8 — Bézier and snapping constants are named constexpr, tested analytically

`kSnapRadiusPx = 12.0`; control-point offset
`δ = max(30.0, 0.5·|x3−x0|)`; `B(0.5)` checked against the hand-computed
analytical point with ±0.5 px tolerance (arc-length parameterization of
`pointAtPercent` justifies the tolerance).

## D9 — `Result<T>` is a local 6-line expected-like struct in `sicnu::workflow`

The repo has no shared `Result<T>`; harness has its own typed errors. D17
defines a minimal `WorkflowResult<T>`-shaped `Result<T>{ok, value, error}` in
`workflow_ir_v2.h` — no dependency, fail-closed parsing, no exceptions across
the seam.

## D10 — `.planning` whitelist

`.gitignore` gains the standard negated-pattern block for
`.planning/workflow-pipeline-designer/*.md` (same as every 10.x track).

## D11 — `gstack` is unavailable as a commit-stack tool on this host

`/usr/sbin/gstack` is glibc's stack-dump utility, not a stacked-git tool; no
`gt`/`branchless` binary exists. The brief's intent (small, semantic, stacked
atomic commits) is honored with plain git: 2–3 focused commits per work
package, conventional-commit subjects referencing the D17 package tag.

## D12 — ADR number 0162

`docs/adr/` ends at 0157 on master; parallel 10.x tracks may take 0158+ in
their own branches. D17 uses **0162** per the brief; a collision on merge is
a docs-only rename (ADR content is authoritative, not its filename).
