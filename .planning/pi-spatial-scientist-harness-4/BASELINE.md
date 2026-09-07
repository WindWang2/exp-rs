# BASELINE — master@a89c0c20 audit (Phase 0)

Three parallel read-only audits were run over the baseline: (A) Pi/MCP/dispatch
path, (B) SpatialTool layer + contracts, (C) workflow/execution backend. This
file records the 12 workflow traces and the gap register that drives the
milestones. All anchors reference master@a89c0c20.

## 1. What already works (baseline strengths)

- **Pi bridge** (`pi/exp-rs-spatial.ts`): spawns desktop binary `--mcp`, MCP
  handshake, category-filtered `tools/list` (default:
  `meta,spatial,data,temporal,cartography,symbology,workflow,workspace`),
  per-tool Pi registration, 50k-char truncation, abort/cancel mapping.
- **MCP server** (`src/agent/mcp_server.cpp`): 21 meta tools + unified catalog
  with compact-by-default listing, `spatial:` etc. inline, `rs:`/`gdal:` via
  ToolCallDispatcher→TaskCenter async, workspace path containment, error
  results with `errorCode/errorCategory/retryable`, `get_lineage`.
- **TaskCenter** (~3k lines): profiles, RSS watermark + RAM budget admission
  (`WaitingResource`), cancel cascades, manual retry, deterministic execution
  cache (RFC 8785 fingerprint), per-task RAM estimates.
- **Workflow Engine v2** (`src/workflow/`): 10-state run FSM, atomic
  checkpoints + flock, partial-failure resume, ArtifactGC, provenance
  sidecars, `resume_workflow` MCP meta tool.
- **~95 operators** (`rs:*` dominant) with JSON param/result seam, determinism
  grades (ADR 0124), memory policies, execution estimates (37 overrides).
- **SpatialToolRegistry**: 60+ inline tools across `spatial:`/`temporal:`/
  `cartography:`/`symbology:`/`workflow:`/`workspace:`/`project:`/`asset:`/
  `lineage:`/`result:`/`run:` namespaces, bounded outputs (pagination, caps).
- **Contracts** (`src/agent/contracts/`): DatasetUnderstanding,
  CapabilityCandidate, PreflightResult, ExecutionPlan, ResultAssessment —
  versioned envelopes with validators.
- **Verification**: OutputVerifier (raster/vector structural, rolled back on
  fail in workflow_runtime), `spatial:assess_result` scientific sanity.
- **Model runtime**: ModelCatalog (26 manifests), `rs:infer` preflight,
  tile inference engine.
- **Governance store** (ADR 0129): SQLite index, runs/results/lineage, 11
  `project:/asset:/collection:/lineage:/result:/run:` tools.

## 2. Twelve end-to-end workflow traces (Phase 0 requirement)

Format: intent → path taken today → verdict (OK / GAP / BROKEN).

1. **List workspace data** — Pi `spatial:workspace_summary` → bounded
   WorkspaceState doc with stable `asset-N`/`layer-N` ids → OK.
2. **Inspect a raster (bands/CRS/roles)** — Pi `spatial:raster_inspect` with a
   caller-supplied **path** copied from workspace_summary → OK but GAP: no
   asset-id resolution; roles only if importer stamped them.
3. **How many bands / which is NIR (governed asset)** — `asset:inspect` gives
   governed CRS/bandCount/modality → OK; but nothing returns a
   DatasetUnderstanding document → GAP (contract dormant).
4. **SAR or optical?** — only via temporal tools (`ObservationContract`) or
   governed assets → GAP: `raster_inspect` has no modality detection.
5. **Pick an algorithm** — `spatial:search_capabilities` ranks the catalog
   with workspace context → OK. `search_algorithms`/`search_tools` overlap →
   duplicate-surface GAP (doc).
6. **Get the schema** — `get_tool_schema`/`get_algorithm_schema` → OK, but the
   agent-visible entry carries **only** name/description/input schema → GAP:
   risk class, side effects, resource hints, cancellation, preconditions,
   expected artifacts, output schema never serialized.
7. **Static DAG check** — `workflow:preflight` (WF_* codes) → OK; but input
   resolution is a path heuristic; no CRS/grid cross-step check → GAP.
8. **Execute a plan** — Pi authors pipeline JSON → `run_workflow` →
   coordinator → TaskCenter → OK. **BROKEN**: MCP-submitted workflow step
   outputs bypass OutputCommitter/asset registration; runId-stamped
   provenance never written for MCP runs (only CLI does). TODO(P1-E1) in
   agent_workflow_executor.cpp:105.
9. **Observe a run** — `get_workflow_status` aggregates steps, `run_id`,
   progress → OK; `resume_workflow` → OK. Task-level WaitingResource maps to
   step "Pending" → minor GAP.
10. **Verify a result** — `spatial:assess_result` (band 1 only) or copilot-only
   OutputVerifier wiring → GAP: MCP execution results carry no `verification`
   block; verdict vocabulary not `PASS/PASS_WITH_WARNINGS/FAIL`; no
   workflow-level aggregate verification.
11. **Final map** — `cartography:compose` → `repair` → `layout:export` is a
   **by-convention** loop → BROKEN: no final map confirmation hook; workflow
   engine has zero cartography awareness; "data ready but map wrong" gap open.
12. **Error handling** — tool errors have codes (`DATA_IO`, `MODEL_NOT_READY`,
   `WF_*`), MCP errors carry `errorCode/errorCategory/retryable` → OK-ish;
   but no stable cross-harness taxonomy, no `suggested_action`, Pi must
   interpret heterogeneous codes → GAP.

## 3. Gap register (drives M2–M7)

| ID | Phase | Gap | Severity |
|----|-------|-----|----------|
| G01 | 1 | Tool metadata (risk/side-effects/resource/cancel/preconditions/artifacts/output schema) exists internally for operators (`AgentMetadata`) but is never exposed to agents | P1 |
| G02 | 1 | No canonical harness catalog view with one stable descriptor per callable; three overlapping discovery stacks (meta tools, catalog, spatial:search_capabilities) | P2 |
| G03 | 2 | Taxonomy implicit in namespaces; no stable `domain.action` classification surfaced | P2 |
| G04 | 3 | Typed context exists (WorkspaceState) but no refresh/invalidation contract tied to project switch/layer change/dataset replacement/workflow completion | P1 |
| G05 | 4 | Grounding tools accept raw paths, not stable asset ids; no `data:understand` producing DatasetUnderstanding; no single-scene modality (SAR/optical) detection | P1 |
| G06 | 5 | No deterministic scientific preflight rule packs (NDVI band roles/radiometry/NoData; change grid/CRS/time-order; SAR polarization/calibration; classification samples/features/model) | P1 |
| G07 | 6/7 | ExecutionPlan contract defined-but-unwired; no plan compiler to WorkflowDefinition; plan lacks goal/inputs/outputs/verification/estimates | P1 |
| G08 | 8 | No plan-level resource estimate (aggregate RAM/seconds/disk) exposed before execution | P2 |
| G09 | 9 | MCP execution results lack `verification`; OutputVerifier verdict not PASS/PASS_WITH_WARNINGS/FAIL; checks band-1 only; no finite-fraction/class-value checks; no workflow-level verification | P1 |
| G10 | 10 | No final map confirmation hook after map-producing workflows | P1 |
| G11 | 11 | Workflow step outputs not registered as governed artifacts on MCP path; no structured run result envelope (`status/run_id/artifacts/warnings/metrics/provenance_id/verification`) | P1 |
| G12 | 12 | No stable cross-harness error taxonomy with recoverable/suggested actions | P1 |
| G13 | 13 | No bounded automatic retry for transient failures; manual retry only | P2 |
| G14 | 14 | No tool risk classification surfaced; no safety vocabulary | P1 (with G01) |
| G15 | 15 | Coordinator run state well-covered; run-level WaitingResource not mapped; per-step verification results not surfaced | P2 |
| G16 | 16 | No metadata-driven scientific recipes (5 recipes required) | P1 |
| G17 | 17 | No Pi subagent role definitions for review/cross-check | P2 |
| G18 | 18 | Benchmark covers tool-level tasks; no scenario evals for the 6 canonical RS workflows with deterministic drivers | P1 |
| G19 | 19 | Entity resolution: ambiguous ids can silently resolve wrong asset (display-name fallback); typed failures not uniform | P1 |
| G20 | 20 | No harness context/schema token-size benchmarks; 512 KiB cap not enforced on live tools | P2 |

## 4. Constraint notes

- Windows local build: MSVC 14.38 + Ninja + vcpkg (shared installed tree from
  `exp-rs-win/build-win`), Qt 6.8 at `C:\deps\Qt`, win_flex/bison, QCA at
  `C:\deps\qca-install`. `configure_build.cmd` in the worktree root.
- `CMAKE_BUILD_PARALLEL_LEVEL=2`, `CTEST_PARALLEL_LEVEL=1` per mission.
- Parallel epics in flight (geospatial-io-4, perf/operator-ization,
  temporal-workspace, win-build) → avoid shared hot files; append-only edits
  to tests/CMakeLists.txt and src/agent/CMakeLists.txt.
