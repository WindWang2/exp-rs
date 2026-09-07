# PLAN — implementation milestones

Build recipe: `configure_build.cmd` / `build.cmd` in worktree root (MSVC 14.38
+ Ninja -j2, vcpkg shared tree). Tests: `ctest --test-dir build -C Release
--output-on-failure` with `CTEST_PARALLEL_LEVEL=1`.

## M2 — Harness spine (Phases 12, 1, 2) — commit `feat(agent): harness spine`

New files `src/agent/harness/`:
- `harness_error.{h,cpp}` — stable `HarnessErrorCode` enum + `HarnessError
  {code, summary, details, recoverable, suggested_actions}`; mappers from
  SpatialToolResult/exception codes; JSON envelope.
- `tool_manifest.{h,cpp}` — `ToolManifest {id, taxonomy, risk_class,
  side_effects, resource_hints, cancellable, preconditions, expected_artifacts,
  output_schema_ref}`; builders from `AgentMetadata` (operators) and a static
  table (spatial tools); bounded `toJson()`.
- `tool_taxonomy.{h,cpp}` — `domain.action` vocabulary
  (data.inspect/search/register/convert, raster.inspect/sample/statistics,
  processing.describe/run, workflow.plan/run/resume, model.inspect/run,
  map.compose/preflight/render, project.inspect/result.inspect/
  provenance.inspect, harness.preflight/plan/execute/verify/recipe);
  classification for every catalog tool.
Surfacing: `mcp_server.cpp` `tools/list`/`get_tool_schema` gain a bounded
`harness` metadata block; `SpatialToolProvider` prefix += `harness:`; MCP
allow-list += `harness:`; taxonomy visible in catalog entries.
Tests: `tests/test_harness_error.cpp`, `tests/test_harness_catalog.cpp`
(metadata present + taxonomy total over the catalog + bounded sizes).

## M3 — Typed context + grounding (Phases 3, 4) — commit `feat(agent): grounding`

- `entity_resolver.{h,cpp}`: resolve `asset-N` / governed uuid / unambiguous
  path / display name; ambiguous ⇒ `ENTITY_AMBIGUOUS` + candidates; unknown ⇒
  `DATASET_NOT_FOUND`. Used by inspect/sample/compare/assess tools (additive
  `asset` param).
- `spatial_context.{h,cpp}` + `harness:context` tool: compact context doc with
  context revision stamps; invalidated by project switch/layer change/dataset
  replacement/workflow completion (revision counters from DataManager
  revisions + workspace entity registry + workflow-run provider seam).
- `data:understand` tool → DatasetUnderstanding contract, incl. modality
  inference (SAR/optical) from GDAL metadata + product metadata.
Tests: `tests/test_entity_resolver.cpp`, `tests/test_spatial_context.cpp`,
extend `tests/test_spatial_tools.cpp` fixtures.

## M4 — Preflight + plan + execution (Phases 5–8) — commit `feat(agent): plan`

- `scientific_preflight.{h,cpp}`: intent rule packs `ndvi|change|sar_change|
  classify|phenology` over resolved inputs (roles from band-role metadata +
  wavelength inference, radiometric state, NoData, grid/CRS/resolution/extent
  comparability, time ordering, polarization/calibration, model-input
  compatibility via ModelCatalog). Output: PreflightResult + HarnessErrors.
  Tool `harness:preflight`.
- `agent_plan.{h,cpp}`: AgentPlan v2 (goal/inputs/steps/outputs/estimates/
  verification/map_output), validator, v1-compat reader.
- `plan_compiler.{h,cpp}`: AgentPlan → WorkflowDefinition (single bridge;
  also used to render estimates).
- `harness:plan` (compile + preflight + estimates), `harness:execute_plan`
  (compile → coordinator startTrackedPipelineJson → run_id).
- Resource estimates: per-step (registry `estimateExecution` style inputs) +
  aggregate RAM/disk/seconds surfaced pre-execution.
Tests: `tests/test_scientific_preflight.cpp`, `tests/test_agent_plan.cpp`.

## M5 — Verification + results + retry + safety (Phases 9–11, 13, 14) —
commit `feat(agent): verification`

- `harness_verification.{h,cpp}`: `Verdict {PASS, PASS_WITH_WARNINGS, FAIL}`;
  `verifyArtifact(path, kind, expectations)`: exists/openable, CRS, dims,
  finite-fraction, class values ⊆ domain, non-empty, extent, workflow state,
  provenance sidecar; aggregate run verdict.
- `plan_runner.{h,cpp}`: watch run → per-step verification → structured
  `PlanRunResult {status, run_id, steps[], artifacts[], warnings, metrics,
  provenance_id, verification}`; bounded transient-retry policy (max 2,
  transient classes only); FAIL ⇒ status failed (no success possible).
- Map confirmation: `map_confirmation.{h,cpp}` invoked when plan declares
  `map_output`; MapSpec preflight/repair loop + layer checks; export gate.
- MCP wiring: `setOutputVerificationHandler` in headless bootstrap;
  `get_workflow_status`/new `harness:run_status` expose per-step verdicts;
  workflow step outputs registered via OutputCommitter/ExecutionPlane seam
  (closes TODO(P1-E1) path for plan runs).
Tests: `tests/test_harness_verification.cpp`, `tests/test_plan_runner.cpp`.

## M6 — Recipes + long-running + Pi (Phases 15–17) — commit `feat(agent): recipes`

- `data/agent/recipes/*.json`: optical-vegetation, optical-change,
  sar-change, land-cover, phenology (metadata only; operator DAGs + slots).
- `recipe_catalog.{h,cpp}` + `harness:search_recipes/describe_recipe/
  instantiate_recipe` → AgentPlan v2.
- `harness:run_status` (full coordinator state incl. WaitingResource),
  docs for cancel/resume via existing meta tools.
- `pi/roles/*.md` + `pi/exp-rs-spatial.ts` category addition `harness`;
  subagent role cards (data-inspector, planner, scientific-reviewer,
  cartography-reviewer, result-verifier) as knowledge docs, single-writer
  discipline documented.
Tests: `tests/test_recipe_catalog.cpp`, instantiate→compile golden tests.

## M7 — Evals + anti-hallucination + token budget (Phases 18–20) —
commit `test(agent): harness evals`

- `tests/test_harness_evals.cpp`: 6 scenarios (NDVI, optical change, SAR
  change, land-cover map, phenology, paper figure) on synthetic fixtures with
  real operators; grade tool selection, param correctness, entity resolution,
  preflight verdicts, plan compile, execution, verification, safety
  (FAIL ⇒ no success), context/token caps. Deterministic, no external LLM.
- Anti-hallucination cases: unknown/ambiguous ids → typed failures everywhere.
- Size benchmarks: catalog bytes, context bytes, schema bytes →
  `benchmarks/harness-token-budgets.json` (best-effort, no CI dependency).

## M8 — Reviews + docs + PR — commit(s) `docs(agent): ...`

- 6 adversarial reviews (architecture, correctness, concurrency/lifecycle,
  performance, cross-platform, docs-claims) recorded in REVIEW_LOG.md; P0/P1
  fixed, P2 evidence-backed.
- Docs: `docs/agent/spatial-scientist-architecture.md` (+5 required docs),
  module README, CONTEXT.md terms + ADR 0130, CHANGELOG entry, PROJECT.md
  refresh, pi/README. Claim-to-code audit for strong words.
- FINAL_REPORT.md; PR `feat(agent): Pi Spatial Scientist & Agent Harness 4.0`.
