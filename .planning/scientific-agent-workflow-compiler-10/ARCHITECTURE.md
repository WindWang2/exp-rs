# ARCHITECTURE — Scientific Workflow Compiler 10.0

ADR: `docs/adr/0149-workflow-compiler-10.md` (written in Phase 1). Everything here
extends the existing harness; nothing replaces an authority listed in BASELINE.md.

## Position in the stack

```
Pi (agent loop, LLM)
  │  authors/edits a TYPED WorkflowIR document (kind "workflow_ir", schema 1.0)
  ▼
harness:compile_workflow        (NEW — the compiler entry)
  │ parse → normalize → ground → static analysis → repair → lower
  ▼
AgentPlan v2 (agent_plan.h — UNCHANGED authority)
  ▼ compilePlanToWorkflowJson (UNCHANGED)
WorkflowDefinition → WorkflowRunCoordinator → TaskCenter  (UNCHANGED engine)
  │ run events
  ▼
harness:diagnose_run + attempt ledger (EXTENDED) → typed repair proposals
```

The compiler is a pure harness-side layer: no second scheduler, no new execution path.
An IR that cannot be lowered to an AgentPlan v2 is a compiler bug, by construction.

## New files (all in `src/agent/harness/`)

| File | Responsibility |
|---|---|
| `workflow_ir.{h,cpp}` | Typed IR: document reader (fail-closed, versioned), deterministic normalize, fingerprint, bounds, artifact-fact merging with provenance |
| `workflow_analysis.{h,cpp}` | Static analysis over normalized IR + resolved facts; typed issues; deterministic order |
| `workflow_repair.{h,cpp}` | Closed repair-rule table; risk-classed insertion; refusals; repair records |
| `workflow_planner.{h,cpp}` | Staged pipeline (intent→ground→candidates→IR→analysis→repair→lower→plan); alternatives; `harness:compile_workflow` tool |
| `context_checkpoint.{h,cpp}` | Session checkpoint/resume/compaction/staleness (file-backed) |
| `tool_shortlist.{h,cpp}` | Deterministic knowledge/tool shortlist with budgets + provenance; `harness:tool_shortlist` tool |

## WorkflowIR document (wire shape, schema_version "1.0")

```
{
  "kind": "workflow_ir", "schema_version": "1.0",
  "ir_id": "wir-<16hex>" (generated when absent),
  "goal": "...", "intent": "ndvi"|"" ,
  "nodes": [{
     "id": "ndvi_1", "operator": "rs:ndvi",
     "params": {...},
     "inputs": [{"node":"src","output":"output","as":"input"}],
     "outputs": [{"name":"output","artifact":{ ...facts... }}],
     "verification": "raster"|"vector"|"skip",
     "resource_estimate_mb": 0, "device": "cpu"|"gpu",
     "determinism": "bit_exact"|"tolerance"|"stochastic"|"",
     "semantic_output": "用户可见的科学产物含义",
     "source": "agent"|"recipe:<id>"|"compose_chain"|"repair:<rule>"
  }],
  "outputs": [{"name":"classified","node":"cls","port":"output","kind":"raster",
               "path":"...", "semantic":"..."}],
  "expectations": {"max_ram_mb":0, "deterministic":true, "device":"cpu"},
  "artifacts": { "<node>.<port>": { ...facts... } }        // optional pre-declared facts
}
```

### Artifact facts (closed key set, all optional)

`kind` (raster|vector|table|model|structured) · `crs` · `grid` {width,height,
pixel_size[2],extent[4]} · `band_roles` {role:count} · `wavelengths_nm` [{band,center,min,max}]
· `numeric_domain` (reflectance|surface_reflectance|toa|dn|db|linear_power|index|categorical|
masked|unknown) · `temporal` {dates[],scene_count} · `modality` (optical|sar|dem|unknown) ·
`sensor` · `dtype` · `nodata[]` · `quality_masks[]` · `polarizations[]` · `calibration` ·
`class_count` (categorical).

Every fact carries provenance in `fact_status`: `observed` (from DatasetUnderstanding) >
`declared` (agent wrote it) > `derived` (capability output contract) > `assumed` > `unknown`.
Merge policy: observed wins over declared for the same key ONLY when they conflict — and the
conflict is itself a typed analysis issue (`FACT_CONFLICT`, severity warning) rather than a
silent overwrite. Missing keys are `unknown`; checks degrade to warnings (facts narrow checks,
never fake them — harness 7.0 rule, kept).

### Bounds (enforced by the reader, fail-closed)

- ≤ 64 nodes, ≤ 8 inputs per node, ≤ 8 outputs per node, node id ≤ 64 chars matching
  `[A-Za-z0-9_.-]+`, goal/semantic strings ≤ 512 chars, `artifacts` ≤ 128 entries.
- Bounds live in ONE table (`irLimits()`) so tests and docs derive from it.

### Deterministic normalize

Canonical node order = topological (stable among independents by id); edge lists sorted;
params object keys sorted (jsoncpp ordered map); fingerprint = SHA-256[:16] over compact
canonical serialization (mirrors `planFingerprint`). Normalize is idempotent:
`normalize(normalize(x)) == normalize(x)` (pinned by test).

## Static analysis (`analyzeWorkflowIr`)

Input: normalized IR + per-input-slot `DatasetUnderstanding` documents (already resolved
through `resolveDatasetRef`/`spatial:understand` — the compiler NEVER opens files itself;
facts come from the grounding stage) + CapabilityKnowledge/CapabilityCatalog entries per
operator + CapabilityRelations + model contracts from ContextLedger/ModelCatalog.

Output: `{verdict: "ok"|"fixable"|"blocked", issues:[{code, severity: error|warning,
node, port, message, repairable, facts}], checks:[...], fingerprint}`. Deterministic
order: issue code, then node id, then port.

Check families (each = one pure function over typed facts; table in
`workflow_analysis.cpp` drives the loop so the set stays closed and testable):

| # | Check | Facts consumed | Issue code (existing unless noted) |
|---|---|---|---|
| 1 | unknown operator | registry + capability knowledge | `INVALID_PLAN` (details.operator) |
| 2 | missing/invalid params | descriptor schema | `INVALID_PARAMETER` |
| 3 | port kind/type mismatch | entry io | `INVALID_PLAN` |
| 4 | modality mismatch | entry.modality × facts.modality | `MODALITY_MISMATCH` |
| 5 | band-role shortfall | entry.band_roles × facts.band_roles | `BAND_ROLE_UNRESOLVED` |
| 6 | wavelength incompatibility | entry band-role wavelength windows × facts.wavelengths_nm | NEW `WAVELENGTH_INCOMPATIBLE` |
| 7 | CRS conflict on multi-raster consumer | facts.crs | `CRS_MISMATCH` |
| 8 | grid conflict (ADR 0098 shared-grid set) | facts.grid | `GRID_MISMATCH` |
| 9 | temporal misalignment (pair/series) | facts.temporal + entry.temporal | NEW `TEMPORAL_MISALIGNMENT` |
| 10 | dB vs linear (SAR) | entry.sar.calibration × facts.numeric_domain | `CALIBRATION_MISMATCH` |
| 11 | DN vs reflectance | entry.radiometric × facts.numeric_domain | `INVALID_RADIOMETRY` |
| 12 | categorical encoding | facts.numeric_domain==categorical × entry | NEW `CATEGORICAL_MISMATCH` |
| 13 | model input compatibility | model contract × upstream facts | `MODEL_INCOMPATIBLE` |
| 14 | resource over-budget | estimates × expectations.max_ram_mb | NEW `RESOURCE_OVER_BUDGET` |
| 15 | output path collision | declared output paths | NEW `OUTPUT_PATH_COLLISION` |
| 16 | non-deterministic chain | catalog stochastic × expectations.deterministic | NEW `NONDETERMINISTIC_CHAIN` (warning) |
| 17 | dangling/cyclic wiring | IR structure | `INVALID_PLAN` (details.reason) |
| 18 | fact conflict | declared vs observed | NEW `FACT_CONFLICT` (warning) |

New codes are APPENDED to `error_codes` + `errorCategoryForCode`/`retryClassForCode`
tables (additive; `isKnownErrorCode` covers them; drift test extended).

## Repair insertion (`planRepairs`)

Closed rule table (`workflow_repair.cpp`), each row:

```
{ rule_id, issue_code, risk_class, requires_facts:[...], operator, param derivation,
  evidence: which facts justify it }
```

| rule | on issue | inserts | risk |
|---|---|---|---|
| `reproject_to_reference` | CRS_MISMATCH | `io:reproject` (target = reference input CRS) | shape_preserving |
| `align_to_reference` | GRID_MISMATCH | `rs:align` (ADR 0098 canonical fixer) | shape_preserving |
| `resample_declared` | GRID_MISMATCH w/ declared target resolution | `rs:resample` | shape_preserving |
| `extract_bands` | BAND_ROLE_UNRESOLVED w/ roles present but mis-ordered | `rs:extract_bands` | shape_preserving |
| `calibrate_reflectance` | INVALID_RADIOMETRY (dn) w/ product metadata present | `rs:radiometric_calibrate` | radiometric |
| `sar_db_to_power` | CALIBRATION_MISMATCH w/ calibration declared | `rs:sar_calibrate` | radiometric |
| `apply_qa_mask` | quality_masks declared + optical index intent | `rs:apply_mask` | science_changing → NEVER auto-inserted; surfaced as decision |
| `temporal_gap_fill` | TEMPORAL_MISALIGNMENT | `rs:temporal_gap_fill` | science_changing → decision only |

Risk classes:
- `shape_preserving`: geometry/format only; auto-inserted with a repair record.
- `radiometric`: changes pixel semantics but is contract-defined; auto-inserted ONLY
  when the metadata facts it needs are `observed`; otherwise becomes a refusal.
- `science_changing`: never auto-inserted; becomes a typed REFUSAL entry
  `{decision_required: true, why, options}` — Pi/user must decide.

Every inserted node gets `source: "repair:<rule_id>"` and the IR document gains a
`repairs[]` record {rule_id, issue_code, inserted_node, facts_used, risk, evidence} and
`refusals[]` {rule_id, issue_code, why, missing_facts, decision_required}. Repair
insertion is deterministic (same IR + same facts → same repaired IR; fingerprint changes
predictably). Repair rules that would depend on broken upstream semantics (F-OPS-3
rs:qa_mask fail-open, F-OPS-4 io:reproject srcCrsOverride) are documented in the rule
table and pinned by tests to not rely on those paths.

## Planner (`compileWorkflow` stages)

Each stage is a pure function returning a typed doc; the stage list is closed:

`intent → grounding → candidates → ir → analysis → repair → lower → plan`

- intent: `resolveGoalIntent` (existing).
- grounding: resolve slot refs through `resolveDatasetRef` + cached/fresh
  DatasetUnderstanding (existing tools) → facts table w/ provenance.
- candidates: `capabilityCandidates` + `solutionPathsForIntent` + `composeChain`
  (existing) — deterministic ranking, `why/why_not`.
- ir: build IR from the chosen candidate (recipe instantiation → IR nodes, or
  agent-supplied IR passed through).
- analysis / repair: above.
- lower: `lowerIrToAgentPlan` — the single IR→AgentPlan v2 bridge (nodes→steps,
  artifact facts → plan inputs/pins, expectations → estimates, verification forward),
  then existing `compilePlanToWorkflowJson`.
- The response carries `stages[]` {stage, status, summary, cost} + alternatives (top-k
  candidate methods with why) + missing facts + limitations. LLM semantic reasoning
  stays upstream (it authors the IR / picks the alternative); everything deterministic
  stays deterministic.

New tools (registered on SpatialToolRegistry — GUI/CLI/MCP/Pi all see them through the
existing mirrors): `harness:compile_workflow`, `harness:tool_shortlist`, and
`harness:workflow_session` (checkpoint/resume/compact/list/stale-report).

## Context checkpoint (`context_checkpoint`)

File-backed sessions under `~/.rs_studio/harness_sessions/` (env
`SICNU_HARNESS_SESSION_DIR` override; same convention as engine's
`~/.rs_studio/checkpoints`). Document = {session_id, schema_version, saved_at, ir
(normalized), analysis, repairs/refusals, plan binding {run_id, plan_fingerprint},
decisions, failed attempts (typed error fingerprints), stage cursor, fact identities
{(path, revision\|(size,mtime)) at save time}, token budget accounting}.

- resume: loads, re-validates facts via stat identity; stale facts → `stale:true` list;
  downstream stages referencing stale facts are invalidated (cursor rewinds to grounding),
  prior decisions/attempts survive (bounded).
- compaction: bounded projection (mirrors run-summary token meter) — artifacts facts
  summarized, band tables dropped, decisions/attempts kept as typed rows.
- bounds: ≤ 8 sessions kept, ≤ 64 KiB per document (trim oldest first).

## Execution/repair loop deepening (`run_loop.cpp` + `context_ledger`)

`harness:diagnose_run` keeps its contract (propose-only) and gains a typed
**attempt ledger** per run (stored via ContextLedger, bounded): {attempt, fingerprint =
H(code, step, structural params), proposal kind, outcome}. Loop policy (typed, bounded):

- `stop: true, reason: "BUDGET_EXHAUSTED"` (existing) — unchanged semantics.
- NEW `stop: true, reason: "REPEATED_ERROR"` when the same error fingerprint would be
  proposed twice (the caller would repeat the same repair — refused).
- NEW `attempt` / `distinct_errors` / `original_intent` echo in every proposal document
  so Pi can never lose the goal across repair attempts.

The full typed loop (preflight → execute → observe → diagnose → repair → bounded retry
→ verify → continue) is DOCUMENTED as the state machine Pi drives through the existing
tools (preflight/execute_plan/run_status/diagnose_run); the harness provides state and
guards, never its own agent loop (ADR 0145 decision 7 preserved).

## Tool/knowledge budget (`tool_shortlist`)

`harness:tool_shortlist {intent?, families?, tags?, limit≤24}` → deterministic page
under a hard 8 KiB budget: {tools:[{id, surface, why_included:{intent/family/tag, score},
summary}], budget_bytes, total_unfiltered}. Reuses `capabilityCatalog.manifestPage`
budget machinery; provenance field explains EVERY inclusion (no silent truncation:
`truncated:true` + count when cut).

## Pi bridge consistency (J)

1. F-PI-2 backport: startup-deadline `try/finally` cleanup in `exp-rs-spatial.ts`
   (mirror of `mcp_bridge.ts:158-172`).
2. F-PI-1: on line-buffer overflow, kill the child process (both files) so the next
   request lazy-respawns instead of zombie-streaming.
3. De-duplication: `exp-rs-spatial.ts` imports the shared `McpBridge` from
   `./mcp_bridge.js` (single implementation; the local copy is deleted).
4. Drift guard `pi/test/no_drift.test.mjs`: (a) exp-rs-spatial.ts must not declare its
   own bridge class/timeout/buffer constants (producer scan), (b) both startup-timeout
   and buffer-overflow behaviors covered by shared tests, (c) the exported tool-list
   surface passes through the shared bridge (no second `tools/list` code path).
   Plus a C++-side floor (`test_workflow_compiler` case) asserting the TS files exist
   and the drift test is wired into pi's `package.json` test script.

## Eval corpus (K)

New category files (append-only) under `data/agent/evals/cases/`:
`workflow_compiler.json` (wrong-CRS repair, dB-to-optical typed failure, missing
metadata refusal, output collision, unknown operator, grid auto-align, over-budget,
normalize idempotence, repeated-error stop), `repair_refusal.json` (science-changing
repairs require decision), `long_context_session.json` (checkpoint→resume→stale
invalidation determinism). Deterministic verdicts wherever the runner executes
deterministically; schema validated by the existing corpus loader.

## What we deliberately do NOT build

- No new plan/engine format: AgentPlan v2 and WorkflowDefinition stay the only
  execution contracts.
- No new agent loop: Pi drives; harness guards.
- No new capability catalog: the 111-operator sidecars stay; we consume them.
- No operator kernels: repair rules compose EXISTING operators only.
- No help-store changes (unified-help-diagnostics-6 lane).
