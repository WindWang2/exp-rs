# RS14-11 Evidence-first Agent Loop — Recon (Phase 0)

Track: `RS14-11-evidence-first-agent`
Branch: `agent/rs14-evidence-first-agent-loop`
Worktree: `exp-rs-wt-rs14-agent-loop`
Baseline: `origin/master` @ `4f6632e1f6` (PR #1145 merged). Re-fetched at start; no open PRs at start. Adjacent RS14 worktrees (`rs14-agent-benchmark`, `rs14-explainable-workflow`, `rs14-inspector-ui`) are still at baseline — no committed overlap. `inspector-ui` has an unmerged ADR commit `0172` (scientific inspector); this track will take ADR **0173** and re-check the number at final union time.

## 1. What already exists (capabilities this track must REUSE, not rebuild)

### 1.1 Harness compiler pipeline (ADR 0149, `src/agent/harness/`)
- `workflow_planner.h`: staged planner `intent → grounding → candidates → IR → analysis → repair → lower → plan`. `compileWorkflow` is the PURE core (consumes fact documents, no side effects). Alternatives carry why/why-not; missing facts and limitations are first-class outputs.
- `agent_plan.h`: versioned AgentPlan v2 (`schema_version "2.0"`), plan fingerprint (SHA-256/16 over scientific content), `compilePlanToWorkflowJson` — the single bridge to the engine, `estimatePlanResources`.
- `workflow_repair.h`: CLOSED repair rule table; risk classes `shape_preserving` (auto-inserted with evidence record), `radiometric` / `science_changing` (NEVER auto-inserted → typed decision-required refusals). Deterministic; every insertion recorded.
- `workflow_analysis.h`: static analysis of IR with severity discipline.

### 1.2 Scientific preflight (Harness 4.0)
- `scientific_preflight.h`: `runScientificPreflight` / `preflightIntent` — verdict `ok | fixable | blocked`; blocked refuses execution; typed HarnessError codes, never prose. `typedIntentDocument` declares required facts BEFORE touching data.
- `spatial_tools/workflow_preflight_tool.h`: preflight tool surface.

### 1.3 Verification & evidence
- `harness_verification.h`: `verifyArtifact` with closed tri-state PASS / PASS_WITH_WARNINGS / FAIL; **a FAIL can never surface as success**; expectations (CRS, dims, band count, nodata fraction, class domain, provenance sidecar, extent, uncertainty).
- `output_verifier.h` (`sicnu::agent::OutputVerifier`): structural health check of committed outputs + teaching grade mode (ADR 0150).
- `evidence.h`: governed sidecar writers `<out>.provenance.json` / `.uncertainty.json` / `.verification.json`; the harness never fabricates uncertainty; engine's own provenance writer wins.
- `operators/runtime/provenance_verify.h`: `verifyProductAgainstModel` — model-anchored provenance verification.

### 1.4 Diagnosis / repair proposal
- `run_loop.h` (`harness:diagnose_run`): reads authoritative run state and emits STRUCTURED repair proposals through the closed action vocabulary; bounded per-run diagnose budget; **never executes a repair — it proposes** ("never a second agent loop").
- `harness_actions.h`: closed suggested-action vocabulary, every action resolves to a registered tool/workbench command; **teaching gate** (D9): student role + lab domain withholds artifact-producing actions with typed `TEACHING_REFUSAL`.

### 1.5 Execution engine (authoritative, untouched by this track)
- `src/workflow/`: `WorkflowRunCoordinator::startTrackedPipeline(Json)` → pipelineId; `WorkflowRun` states; checkpoint manager. Library `sicnu_workflow` (SHARED, Qt-Core).
- `src/processing/framework/task_center.h`: `TaskCenter` scheduler (`sicnu` namespace); submit/cancel/poll; resource budgets (`task_resource_budget*.h`). Library `sicnu_task_center` (STATIC, links `sicnu_workflow`).
- `harness:execute_plan` (`plan_tools.cpp`): pins → preflight gate → compile → `startTrackedPipelineJson` → run_id/pipeline_id; observe via `harness:run_status`; transient failures retried via bounded resume.
- `processing/framework/agent_workflow_executor.h`: `executeAgentPlan(Json)` blocking driver (precedent for adapter shape).

### 1.6 Mission runtime (13.0, ADR 0166, `src/app/workbench/` + `src/agent/spatial_tools/mission_tools.*`)
- Value model `MissionContext` / `MissionTimeline` / `MissionTask` / `MissionRunRef` / `MissionStage {Import, Preprocess, Analyze, Verify, Publish}`.
- `applyMissionAction(taskId, action, ...)` — the single mutation entry (start|succeed|fail|cancel|retry|bind_run|unbind_run|reconcile). `Pending → Running` requires a bound run ref that resolves Alive.
- **Gap found: nothing submits work to TaskCenter from the mission layer** — the mission records WHO runs a task; it never launches it. (An orchestration layer is the natural filler, via adapters only.)
- No `sicnu_mission` CMake target: mission Qt-Core sources compile into `sicnu_agent` (DLL) + `sicnu_geo_rs` (exe) — exactly one of the two on Windows.

### 1.7 Session/continuity infrastructure
- `context_checkpoint.h`: `HarnessSessionState` + `HarnessSessionStore` — file-backed resumable compiler context, stage cursor (`intent → grounding → candidates → ir → analysis → repair → lower → executing → verifying → done`), staleness detection (path/size/mtime/revision), bounded store (8 sessions, 64 KiB/doc), atomic writes. **This is the compiler context, NOT an orchestration journal.**
- `context_ledger.h`: `ContextLedger` — bounded plan bindings, typed decisions (kind/subject/status/note/candidates), asset contexts, model contracts, evidence-aware run summaries.
- `harness:decision_record` tool (`grounding_tools.cpp`): thin manual ledger over ContextLedger (record/resolve/list). Limited shape; **not** an orchestration DecisionRecord.
- `harness_error.h`: stable error-code taxonomy + retry classes (None/Manual/Transient) + error envelopes.

### 1.8 Build & test infrastructure
- Presets: `dev-default` → `build-dev` (Unix Makefiles, Debug, ENABLE_TESTS=ON). Build one target: `cmake --build build-dev --target <t>`; test: `QT_QPA_PLATFORM=offscreen ctest -R <t> -j1 --output-on-failure` (env pinned by `cmake/SicnuTestEnv.cmake`).
- Catch2 v3.7.1. Pure-lib precedent: `src/contracts/CMakeLists.txt` (STATIC, C++20, jsoncpp, no Qt). Test registration pattern: hand-rolled `add_executable` + `Catch2::Catch2WithMain` + `sicnu_discover_tests` (see `test_tool_call_dispatcher`).
- Resource rules honored: `-j2` max, no parallel build targets, no full rebuilds.

## 2. The gap (what this track builds)

There is **no orchestration layer** that runs the full loop
`Goal → Understand → Plan → Preflight → RepairApproval → Execute → Verify → Diagnose → Replan → Delivery`
as one bounded, evidence-first session:

1. No session state machine that owns cross-stage transitions and a journal of *why* each decision was taken.
2. No structured `DecisionRecord` (inputs / alternatives / selected action / reason / evidence / policy) — ContextLedger's decision rows are a manual, shallower ledger.
3. No cross-stage budget enforcement: max replans, resource budget vs plan estimates, no-progress (repeated identical failure) detection, abort.
4. No uniform run modes: dry-run / plan-only / execute-with-verify.
5. No deterministic no-LLM E2E of the whole loop (the harness planner needs an authored IR/recipe; there is no offline planner fixture for the loop).
6. No machine-readable evidence summary of a session for future agents (GUI-first surfaces today).

Everything the loop NEEDS at each stage already exists (planner, preflight, repair rules, verifier, diagnoser, engine). This track composes them behind interfaces and adds the missing orchestration, journal, budgets, and evidence projection. It re-implements no science and no scheduler.

## 3. What this track explicitly does NOT do

- Does NOT fix any open issue (see the avoid-list; #1146–#1187). Observations are recorded as "blocked/observed" only.
- Does NOT modify Mission / Workflow / TaskCenter sources. Integration is via adapters calling their public APIs.
- Does NOT create a second scheduler, second registry, second provenance store, or second experiment store. The journal is a session-scoped projection; provenance/verification facts stay owned by their existing writers.
- Does NOT re-implement the harness planner, preflight rule packs, repair rule table, verifier, or diagnoser. The production adapter delegates to them.
- Does NOT ship GUI business logic; any surface is a thin tool over the core service.
- Does NOT act as "a second agent loop" that executes repairs itself (`run_loop.h` constraint): the session *drives* seams and records decisions; the executor seam is the only thing that runs work.

## 4. Extension seams chosen

| Seam | Existing anchor | Adapter plan |
|---|---|---|
| Planner | `harness::compileWorkflow` / `AgentPlan` + `planFingerprint` | production adapter compiles an IR/recipe-backed plan; offline fixture planner in core for E2E |
| Data state | `spatial:understand` facts / `resolveDatasetRef` / `ContextLedger` understanding cache | provider seam returns a bounded snapshot of slot facts |
| Preflight | `preflightIntent` (verdict ok/fixable/blocked) | adapter maps verdict into session transitions; blocked → Refused |
| Repair approval | `repairRuleTable` risk classes + `harness_actions` teaching gate | session applies an explicit RepairApprovalPolicy (auto-approve only declared-safe classes; everything else is a recorded decision) |
| Executor | `WorkflowRunCoordinator::startTrackedPipeline(Json)` + poll `harness:run_status`-equivalent | adapter begins a run and polls with a bounded timeout; fakes in tests |
| Verifier | `verifyArtifact` / evidence sidecars / `verifyProductAgainstModel` | adapter verifies declared outputs; fakes in tests |
| Diagnoser | `harness:diagnose_run` structured proposals | adapter turns failure evidence into typed proposals; fakes in tests |
| Persistence | `HarnessSessionStore` conventions (atomic write, bounded, stale detection) | journal follows the same conventions with std C++ only |

## 5. Dedup matrix (dynamic, vs master @ 4f6632e1f6 + open issues + sibling tracks)

| Concern | Existing owner | This track's delta | Verdict |
|---|---|---|---|
| Plan compilation | harness `compileWorkflow` | reuse via seam; no recompile logic | no dup |
| Preflight | `preflightIntent` | reuse via seam | no dup |
| Repair insertion | `workflow_repair` rule table | session only *approves/records*; never inserts | no dup |
| Verification | `verifyArtifact` / OutputVerifier / provenance_verify | reuse via seam | no dup |
| Diagnosis | `harness:diagnose_run` | reuse via seam | no dup |
| Execution | WorkflowRunCoordinator / TaskCenter / execute_plan | reuse via seam; never a scheduler | no dup |
| Decisions | `ContextLedger` + `harness:decision_record` (manual, shallow) | NEW `DecisionRecord` type for the session journal (richer, orchestration-scoped). No writes into ContextLedger → no second source; documented boundary | new, non-overlapping |
| Session persistence | `HarnessSessionStore` (compiler context) | NEW journal (orchestration evidence, append-only, replayable). Different subject; same file conventions | new, non-overlapping |
| Budgets | TaskCenter budgets / estimatePlanResources | NEW session-level policy over plan estimates (consumes estimates; owns no engine gate) | new, non-overlapping |
| Agent tool surface | `harness:*`, `spatial:*`, `mission:*` | NEW `scientific:agent_session` tool (projection of the core; parity-gated surfaces respected) | new |
| Mission run launch gap | mission records runs, never launches | NOT filled here (mission stays read-only to this track; adapter observes only) | out of scope |
| #1168/#1169/#1170 (mission) | open issues | observed, not touched | dedup: avoid |
| #1159/#1182 (taskcenter) | open issues | observed, not touched | dedup: avoid |
| #1181 (tool catalog cache) | open issue | new tool registration is static at startup → not affected | dedup: avoid |
| #1179 (test oracle potency) | open issue | this track's oracles must be non-vacuous (self-checked) | respected |
| inspector-ui ADR 0172 | unmerged sibling | take 0173; re-check at union | boundary |

## 6. Risks

- **R1 Qt-free core vs Qt-heavy adapters**: the core must stay pure C++20 + jsoncpp so unit tests link in seconds. Production adapters live in `sicnu_agent` (Qt-capable). Mitigation: seam interfaces are pure virtual with value types only.
- **R2 Windows duplicate-symbol hazard** for mission Qt-Core sources: this track adds no mission sources → unaffected.
- **R3 Drift gates**: new tool surfaces can trip capability/contract tests (some already red on master, e.g. #1187). Mitigation: register through the standard registry, update only test-owned drift tables, never weaken an oracle.
- **R4 Atomic file write on Windows**: journal uses temp+rename with remove-first fallback; the known rename-over-open issue (#1178) is recorded as observed, not fixed.
- **R5 Determinism**: all clocks are injected; the offline planner is a closed table; replay reconstructs from the journal alone.

## 7. Interfaces to sibling tracks (19 concurrent directions)

- Stable C++ interfaces (`session_seams.h`) + value objects; fakes complete the TDD without any sibling PR merged.
- New schema versioned (`schema_version` on every persisted document).
- No dependency on another track's unmerged types; the integration seam is documented in `docs/integration.md`.
