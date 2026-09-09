# FINAL_REPORT — Dataset / Experiment / Reproducibility Platform 7.0

Branch: `feat/dataset-experiment-7` (worktree `exp-rs-dataset-experiment-7`)
Base: master @ `2041f6fa`

## Architecture

All additions are thin over the authoritative stores (`DatasetStore`,
`ExperimentStore`) and the existing engines (split, leakage, evaluation,
comparison, bundles). No second scheduler, no new persistence format; store
schema v1 extended with additive tables only.

New units:
- `src/dataset/dataset_store_splits.cpp` — split manifest + leakage report persistence
- `src/dataset/sample_promotion.{h,cpp}` — pipeline → SampleRecord promotion (goal B)
- `src/dataset/fold_audit.{h,cpp}` — per-fold audit/comparability/replay (goal D)
- `src/dataset/dataset_store_facets.cpp` — facet side table + quality cache (goal E)
- `src/experiment/run_recorder.{h,cpp}` — Workflow/TaskCenter → ExperimentRun (goal C)
- `src/experiment/replay_readiness.{h,cpp}` — replay readiness + equivalent runs (goal F)
- `src/experiment/comparison_ext.{h,cpp}` — protocol/schema compatibility + paired runs (goal G)
- `src/agent/data_platform_tools.{h,cpp}` — 15 MCP tools (goal A)
- CLI verbs in `src/cli/cli_dataset_commands.cpp` (goal A)

## Changes

(filled at PR time — see PR description)

## Tests

- `tests/test_data_platform_surface.cpp` — split/leakage persistence, MCP
  projections, honest readiness levels (5 TEST_CASEs + M1 surfaces)
- `tests/test_platform7_library.cpp` — promotion, recorder, fold audit,
  facets/quality cache, replay readiness, comparison extensions (15 TEST_CASEs)
- Evidence: local build + ctest runs (paths/commands in REVIEW_LOG)

## Performance / resources

- Facet queries are SQL GROUP BY with bounded results + "(other)" tail.
- Audit assembly is paged (500/page); leakage checks stay bucketed.
- Build at j2/j6 with observed RAM headroom; tests at CTEST_PARALLEL_LEVEL=1.

## Compatibility

- Additive store tables; schema_version stays "1" (older readers unaffected;
  newer tables ignored). No API breaks: all changes are new symbols plus the
  mcp_server dispatch branch (first-match, so existing tools unaffected).

## Known limitations

- CLI/MCP share verbs but not projection code (CLI links jsoncpp and cannot
  link the GUI-level agent lib without an architecture regression).
- Recorder is not yet called from every workflow state hook; it is the
  callable adapter at the seam (integration points documented).
- `get_tool_schema` (Agent Tool Catalog) does not resolve dataset:/experiment:
  tools; schemas come from tools/list includeSchemas=true.
- Near-duplicate detection requires caller-supplied digests; the platform
  never pretends a perceptual hash it did not compute.

## Follow-ups

- Wire `ExperimentRunRecorder` into WorkflowRunCoordinator state transitions.
- MCP export tool: progress streaming for portable bundles.
- Fold comparability: add stratification-aware balance suggestions.
