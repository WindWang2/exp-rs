# ADR 0128: Spatial Scientist 3.0 — Structured Agent Contracts & Capability Surfaces

- Status: Accepted (implemented in this branch)
- Context: exp-rs' Pi bridge (ADR 0122) exposed tools, but planning inputs were
  prose-heavy: workspace dumps, unranked search, and no static workflow feasibility
  checks. Pi had to reason over free text to answer "what is here", "what should I
  run", "was the result sane".

## Decision

exp-rs stays the **capability provider** (no agent runtime in C++); what is added are
structured, versioned, bounded JSON surfaces:

1. **Contract documents** (`src/agent/contracts/`): DatasetUnderstanding,
   CapabilityCandidate, PreflightResult, ExecutionPlan, ResultAssessment,
   MapQualityReport — one envelope, one schema version, validators included.
2. **Workspace Understanding 3.0** (`workspace_state.{h,cpp}`): stable entity ids
   (`asset-3`, `layer-2`, chart/layout/task ids) resolved against natural keys and
   persisted in project properties; visible/selected/active layers, recent outputs,
   running tasks, workflow runs (via a provider seam — the agent library must not
   link the workflow engine).
3. **Capability discovery & model selection** (`spatial:search_capabilities`,
   `spatial:select_model`): ranking fuses catalog facets with workspace context
   (band roles, CRS, resolution, GPU) and returns CapabilityCandidate summaries —
   never full schemas. The agent expresses the *task contract*, not model names.
4. **Static workflow preflight** (`workflow:preflight`): WF_-prefixed error codes
   with repairability flags so planners fix DAGs before executing.
5. **Result assessment** (`spatial:assess_result`): nodata ratio, constant-output
   suspicion, range/class expectations, geometry validity — scientific sanity before
   a success status is trusted.
6. **Cartography surfaces** (`cartography:*`, `symbology:*`): MapSpec compose /
   preflight / repair loop, component + template libraries, charts as first-class
   components (QGIS-native `QgsLayoutItemChart` for layer-bound series, QPainter
   renderer for inline data — no QtCharts), and renderer tools whose mutations are
   undoable through the WorkspaceCommandStack (`workspace:undo/redo`).
7. **Benchmark** (`tests/test_spatial_scientist_benchmark.cpp`, 111 tasks): the
   contract is graded against live registries deterministically.

All tools remain `SpatialTool`s inside `SpatialToolRegistry` (single tool path for
MCP/copilot/CLI), bounded by pagination caps and compact-by-default responses.

## Consequences

- Pi reasoning shifts from parsing prose to reading documents; each document carries
  machine-actionable repairs (`suggested_action`) instead of error prose.
- Tool-count growth is bounded: new capabilities extend ranking facets and contract
  validators, not new ad-hoc tool families.
- Layout/QGIS duplication is explicitly avoided; MapSpec compiles through the same
  property layer the GUI uses.
