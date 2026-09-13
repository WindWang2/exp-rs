# Scientific Workflow Compiler 10.0 — typed WorkflowIR, static analysis, contract-driven repair

**Local evidence only; no online CI dependency.** Every claim below maps to a
local command + exit code recorded in
`.planning/scientific-agent-workflow-compiler-10/EVIDENCE.md`.

## Baseline

- `origin/master @ 7d78059d1a` (unchanged at PR time; no rebase needed).
- Direct predecessors consumed, not duplicated: AgentPlan v2 (ADR 0130-harness),
  capability knowledge (ADR 0142), harness 8/9 (ADR 0144/0145), capability
  sidecars + relation graph (ADR 0146), eval corpus 9.0.

## Architecture (ADR 0149, `docs/adr/0149-workflow-compiler-10.md`)

`harness:compile_workflow` — staged compiler over a **typed WorkflowIR 1.0**
(`src/agent/harness/workflow_ir.*`):

```
parse → ground → candidates → analysis → repair → lower
   (IR)   (facts)  (ranked alt.)  (17 checks)  (risk-classed)  (AgentPlan v2)
```

The IR is a versioned, bounded, fail-closed, serializable document whose nodes
carry typed ports and artifact facts (kind, CRS, grid, band roles, wavelengths,
numeric domain, temporal domain, modality/sensor, determinism grade, resource/
device estimates, provenance expectation, semantic output). Every fact carries
`fact_status` provenance (observed > declared > derived > inherited-assumed).
Lowering goes IR → AgentPlan v2 → the existing `compilePlanToWorkflowJson` —
one bridge in, one bridge out; the compiler never executes and never opens data.

## Major deliverables

1. **WorkflowIR 1.0** — fail-closed reader (closed fact vocabulary, id/port/
   text bounds, duplicate-port rejection), deterministic idempotent normalize
   (stable topological order), plan-parity fingerprint, structure validation
   (dangling edges, missing ports, cycles), fact merge with conflict records.
2. **Static analysis** (`workflow_analysis.*`) — 17 ledger check families over
   typed facts: unknown operator, missing params, required ports (edge- or
   params-style), port-kind conflicts, modality, band roles, wavelength windows,
   CRS conflicts (case-folded, both fact keys), shared-grid (ADR 0098),
   temporal coverage, dB/DN/categorical numeric domain, model compatibility,
   resource budget, output-path collisions, non-determinism warnings, fact
   conflicts. Severity discipline: assumed-only facts degrade to warnings —
   never fake errors, never fake passes (shape-tolerant grid facts, tri-state
   grid compare). Seven additive taxonomy codes
   (`WAVELENGTH_INCOMPATIBLE … FACT_CONFLICT`).
3. **Deterministic repair insertion** (`workflow_repair.*`) — closed rule
   table with risk classes: `reproject_to_reference` / `align_to_reference`
   (shape-preserving, auto-inserted with evidence records);
   radiometric/science-changing repairs become **prepared decision refusals**
   (calibration, SAR DN calibration with metadata guard, QA-mask opportunity,
   gap fill, dataset substitution). Silent science changes are structurally
   impossible; every insertion changes the fingerprint and is recorded.
4. **Staged planner** (`workflow_planner.*`) — per-stage reports, alternatives
   with deterministic ranking, missing facts, limitations; recipe→IR conversion
   (recipes compile through the same pipeline); IR→AgentPlan lowering with
   derived output paths and grounded slot wiring; compiler provenance (ir id/
   fingerprint/repairs/refusals) rides into the run binding
   (`harness:execute_plan` → ContextLedger) and `harness:explain`.
   **Execution gate**: a verdict ≠ ok withholds the engine JSON.
5. **Harness session checkpoint** (`context_checkpoint.*`,
   `harness:workflow_session`) — file-backed sessions (engine-convention
   `~/.rs_studio/harness_sessions/`), atomic writes, 8-session/64KiB bounds,
   compaction, resume with stale-fact invalidation (stat identities) that
   rewinds to grounding while decisions/attempt history survive.
6. **Repair-loop guard** (`run_loop.cpp`) — per-run proposal-set signatures:
   an unchanged re-diagnosis stops with `repeated_error`; every terminal
   diagnosis echoes `original_intent` and `distinct_proposal_sets`. Loop stays
   Pi-driven (ADR 0145 decision 7).
7. **Knowledge budgeting** (`tool_shortlist.*`) — deterministic
   provenance-carrying `harness:tool_shortlist` under a hard 8 KiB page
   (adversarial-filter proof) + `harness:knowledge_budget` report.
8. **Pi bridge** — single shared `McpBridge` (the extension shell imports it);
   F-PI-1 fixed (runaway line kills the child → lazy-respawn recovery, plus
   stale child event/data guards), F-PI-2 fixed (startup deadline cancelled on
   every settle path), circuit breaker absorbed into the shared class;
   `pi/test/no_drift.test.mjs` (static + behavioral) prevents re-forking.
9. **Eval corpus** — 3 new closed categories (`workflow_compiler`,
   `repair_refusal`, `knowledge_budget`; 8 deterministic cases) registered in
   the runner's enforced list.

## Compatibility

- Additive only: 7 new error codes (closed table extended), 3 new corpus
  categories, new tools registered through the existing SpatialToolRegistry —
  GUI/CLI/MCP/Pi see them via the existing mirrors.
- No engine/plan format changes: AgentPlan v2 and WorkflowDefinition remain the
  only execution contracts. `compilePlanToWorkflowJson` output unchanged.
- `ContextLedger::recordPlanBinding` gains an optional trailing parameter
  (existing callers unaffected). `harness:diagnose_run` gains fields; its
  budget semantics are unchanged.

## Tests (all local, serial, offscreen; commands + exit codes in EVIDENCE.md)

New: test_workflow_ir 89/8 · test_workflow_analysis 125/18 ·
test_workflow_repair 102/9 · test_workflow_planner 50/6 ·
test_context_checkpoint 75/6 · test_tool_shortlist 40/7.
Regression: harness_error 42/4 · harness_eval_corpus 756/2 ·
harness9_contracts 289/11 · harness_grounding 126/7 · harness_evidence 58/7 ·
harness_evals 269/17 · agent_tools_3 126/6 · spatial_contracts 67/9.
Pi: node --test → 9/9 (incl. behavioral flood-recovery test).

## Performance / resource

Bounds are pinned by tests, not wall-clock gates: IR ≤64 nodes (reject tested),
8 KiB shortlist page (adversarial-filter test), 64 KiB session documents
(compaction + load-rejection tests), budget report measures every surface.
Builds strictly `-j2` (`CMAKE_BUILD_PARALLEL_LEVEL=2`), tests serial.

## Review findings

Two read-only adversarial reviewers (architecture/science; tests/bounds/
pi/docs) found 5 P0 + 11 P1 + P2/P3s. **All P0/P1 fixed** (store mutex
self-deadlock, repair-rule use-after-free, grid-shape fake-pass,
assumed-fact error gating, execution gate, CRS normalizer unification,
eval hermeticity, corpus category registration, test-contract mismatches);
P2/P3 fixed or dispositioned with rationale — full ledger in
`.planning/.../REVIEW_LOG.md`.

## Known limitations

- `test_capability_drift` has 3 failures **pre-existing on master** (verified
  at baseline): `rs:gaofen/zy3/hj_import` + `cartography:diff_templates/
  explain/export` lack knowledge entries (merged by other tracks),
  `harness.optical_ndvi_landsat` alias surfaces in listRecipes. Generated-data
  fixes belong to the D8/cn-product lanes (hand-editing generated sidecars is
  drift per ADR 0146).
- Radiometric repairs are prepared decisions, not auto-insertions — the real
  knowledge contracts grade DN as warn-class, so no fact-backed auto-insertion
  path exists (recorded in ADR 0149 decision 4).
- `harness:compile_workflow` grounds live inside tool execute (same cost as
  `spatial:understand`); an async seam is an execution-plane follow-up.

## Follow-ups

1. Execution-plane: consume `metadata.workflow_ir` from the compiled workflow
   for engine-side provenance surfacing.
2. D8 lane: regenerate capability sidecars/knowledge for the CN import
   operators + cartography tools (clears the pre-existing drift failures).
3. WorkflowIR fact model: temporal alignment beyond min_scenes (calendar
   regularity) once the temporal track's contracts land.
4. Compiler eval cases exercising live GDAL grounding end-to-end (fixture
   with richer product metadata).

## Local evidence only; no online CI dependency

No GitHub Actions run was triggered, awaited, or cited. All verification is
reproducible locally: `cmake --preset dev-default`, targeted
`cmake --build build-dev -j2 --target <suite>`, `ctest`/direct Catch2 runs
with `QT_QPA_PLATFORM=offscreen`, `cd pi && node --test test/`.
