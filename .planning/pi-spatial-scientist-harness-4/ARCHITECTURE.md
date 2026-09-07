# ARCHITECTURE — Harness 4.0

## Layering (all new code under owned dirs)

```
Pi (generic: loop/planning/memory)                    pi/  (TS bridge + knowledge)
─────────────────────────────────────────────────────
ExpRS Spatial Harness                                 src/agent/harness/          ← NEW
  HarnessError taxonomy            (harness/error_codes.h)          G12
  ToolManifest: risk/scan/estimates/artifacts      (harness/tool_manifest.*)        G01/G03/G14
  EntityResolver: asset/layer/collection ids       (harness/entity_resolver.*)      G05/G19
  SpatialContext: typed, rev-stamped               (harness/spatial_context.*)      G04
  ScientificPreflight: rule packs                  (harness/scientific_preflight.*) G06
  PlanModel v2 + PlanCompiler → WorkflowDefinition (harness/agent_plan.*)           G07/G08
  PlanRunner: execute + bounded retry + verify     (harness/plan_runner.*)          G09/G11/G13
  OutputVerification: PASS/PASS_WITH_WARNINGS/FAIL (harness/verification.*)         G09
  MapConfirmation hook                             (harness/map_confirmation.*)     G10
  RecipeCatalog                                    (harness/recipe_catalog.*)       G16
─────────────────────────────────────────────────────
Existing seams (extended additively)
  SpatialToolRegistry  + new `harness:*` tools     src/agent/spatial_tools/
  Contracts v2 (plan/verification/errors)          src/agent/contracts/
  Catalog metadata surfacing                       src/agent/tool_catalog/, mcp_server.cpp
  Grounding (asset-id resolution, DatasetUnderstanding) spatial_tools/raster_inspect etc.
─────────────────────────────────────────────────────
Authoritative engines (unchanged)
  WorkflowRunCoordinator → TaskCenter → RSOperator   src/workflow/, src/processing/, src/operators/
  OutputCommitter / DerivationRecord / Governance    src/processing/, src/data/
  MapSpec compiler / LayoutService / QgsPrintLayout  src/agent/mapspec/, cartography/
```

## Key decisions

### D1 — Harness is a library inside `sicnu_agent`, exposed as `harness:*` SpatialTools
New namespace `harness:` joins `spatial:`, `workflow:` etc. in
`SpatialToolProvider` prefixes and the MCP allow-list. All new agent surface =
`SpatialTool`s (inline, bounded, JSON in/out), consistent with ADR 0122.
Pi-visible categories gain `harness` in `pi/exp-rs-spatial.ts`.

### D2 — One error taxonomy, one verdict vocabulary
`HarnessError {code, summary, details, recoverable, suggested_actions[]}` with
the stable code set from the mission (`DATASET_NOT_FOUND`,
`BAND_ROLE_UNRESOLVED`, `CRS_MISMATCH`, `GRID_MISMATCH`,
`INVALID_RADIOMETRY`, `INSUFFICIENT_MEMORY`, `MODEL_INCOMPATIBLE`,
`EXECUTION_FAILED`, `CANCELLED`, `OUTPUT_INVALID`, `MAP_PREFLIGHT_FAILED`, …).
Verification verdicts: `PASS | PASS_WITH_WARNINGS | FAIL` (Phase 9); preflight
verdicts keep the 3.0 `ok | fixable | blocked` and map onto the same tri-state.
FAIL anywhere ⇒ plan result status `failed`; Pi must not report success.

### D3 — Plans compile; they never fork the engine
`AgentPlan` v2 (`schema_version 2.0`): `{plan_id, goal, inputs[]{ref, entity},
steps[]{id, operator_id, params, inputs[]{step,port}, outputs, estimates,
verification_policy}, outputs[]{name, from_step, artifact_kind}, verification,
map_output?}`. `PlanCompiler::compile(plan) → WorkflowDefinition` is the only
execution bridge (`WorkflowRunCoordinator::startTrackedPipeline`). No second
scheduler; concurrency stays in TaskCenter admission.

### D4 — Grounding = resolve → inspect → typed document
`EntityResolver` resolves `{asset id | asset-N | governed uuid | path}` against
DataManager + governance store; ambiguous name matches return typed failure
`ENTITY_AMBIGUOUS` with candidates (never silent pick). Inspect tools gain
optional `asset` param; new `data:understand` returns the DatasetUnderstanding
contract (wired version of the dormant 3.0 adapter) incl. single-scene
modality inference (SAR/optical from driver/metadata/polarization hints).

### D5 — Scientific preflight is rule packs, not LLM
`ScientificPreflight::evaluate(intent, resolved_inputs)` runs deterministic
checks per intent family: `ndvi` (NIR/Red roles resolvable, radiometric state,
NoData), `change` (grid/CRS/resolution/extent/radiometry/time ordering),
`sar_change` (polarization, calibration domain, orbit/geometry, grid),
`classify` (training samples/classes/features/model compatibility),
`phenology` (collection time ordering, QA coverage, index band roles).
Output: PreflightResult contract + HarnessError codes; `blocked` ⇒ plan
refused — the plan runner will not submit.

### D6 — Verification is automatic and structured
`plan_runner` verifies every declared output post-run: OutputVerifier checks +
finite-fraction, class-value domain, extent-vs-input, plus workflow-state and
provenance-sidecar presence. Aggregate run result:
`{status: ok|failed, run_id, steps[]{step_id, status, verdict}, artifacts[]{name, asset_id, path, verification}, warnings[], metrics, provenance_id}`.
MCP `get_execution_status` gains the verification block via the existing
`setOutputVerificationHandler` seam (wired in mcp_server headless bootstrap).

### D7 — Map confirmation closes the loop
If a plan/recipe declares `map_output`, after workflow completion the runner
invokes `MapConfirmation::confirm`: MapSpec/preflight/repair on the target
layout, layer-exists/visible/ordering checks, legend presence, off-page,
intermediate-layer cleanliness → MapQualityReport + PASS/WARN/FAIL; export
only on non-FAIL. Implemented through the existing cartography seams
(MapSpecCompiler, cartography preflight/repair) — no second layout engine.

### D8 — Retry is bounded and class-safe
`PlanRunner` retries only `retryable`-classified transient failures
(worker startup, transient I/O) with a max-attempt policy (default 2 retries,
exponential-ish backoff, persisted in run record). Non-idempotent or
project-mutating failures never auto-retry. LLM never sets retry counts.

### D9 — Recipes are metadata that compile to plans
`data/agent/recipes/*.json` describe the 5 mission recipes as operator DAGs
with slot bindings (`primary`, `secondary`, `training`, `collection` …).
`recipe:describe`/`recipe:instantiate` produce AgentPlan v2 documents; kernels
are never copied. Eval scenarios run recipes end-to-end on synthetic
GeoTIFFs via real operators.

### D10 — Token budget is a tested contract
Every harness tool response ≤ 16 KiB by default (hard cap 512 KiB enforced at
the registry seam); catalog listing stays compact-by-default; staged discovery
(`harness:search_tools` → `get_tool_schema` → invoke) documented and benched
in `tests/test_harness_evals.cpp` (size assertions) and a benchmark JSON.

## Compatibility

- All new tools additive; existing tool names/schemas unchanged.
- `spatial:raster_inspect` etc. gain optional `asset` field — additive.
- MCP `tools/list` entry gains optional bounded `harness` metadata block —
  additive for clients (Pi passes it through).
- Plan schema versioned; v1 ExecutionPlan documents still validate (v1 stays
  a subset; compiler accepts both).
