# PLAN — milestone order & verification

Order chosen so each milestone lands contract-first with tests, smallest blast
radius first. M1 touches only additive CLI/MCP surfaces; M2/M3 are the deepest
(mission-critical adapters); M4–M7 are library additions.

| # | Milestone | Files | Verification |
|---|---|---|---|
| M0 | Build env up in worktree | build-dev/ | configure + build dataset/experiment/cli targets, run existing dataset/experiment tests green |
| M1 | MCP surface + CLI completion | `src/agent/mcp_tools_data.*`, `src/cli/cli_dataset_commands.cpp`, shared projection `src/experiment/*` as needed | new `test_mcp_data_surface` + extended CLI tests; every tool: paged, bounded, JSON-schema'd output |
| M2 | Pipeline→SampleRecord promotion | `src/dataset/sample_promotion.*` | `test_sample_promotion`: classification raster→polygons (row-index never becomes class identity), segmentation→objects, annotation chains, pairs/temporal, draft-only writes, provenance recording |
| M3 | Workflow→ExperimentRun recorder | `src/experiment/run_recorder.*` (+1 call-site seam) | `test_run_recorder`: lifecycle created→running→succeeded/failed/cancelled; crash leaves truthful state; artifacts/metrics; secret-filtered env |
| M4 | Fold-level audit & comparability | `src/dataset/fold_audit.*` | `test_fold_audit`: planted leakage found in the right fold only, zero-ratio folds flagged, deterministic replay fingerprint equality |
| M5 | Facets & scale | `src/dataset/dataset_facets.*`, store indexes | `test_dataset_facets`: 100k synthetic rows, bounded memory (paged queries), facet totals == row count, stale-cache detection |
| M6 | Replay readiness | `src/experiment/replay_readiness.*` | `test_replay_readiness`: each dependency missing/mismatched/ok; level never overstates; secret scan of exports |
| M7 | Comparison extensions | `src/experiment/comparison_ext.*` | `test_comparison_ext`: known comparable/non-comparable cases; protocol & schema diffs; paired deltas; insufficient-n honesty |

Then: REVIEW (≤2 subagents, adversarial, read-only) → remediate → sync master →
full local regression of touched targets → PR.

## Commit policy

Small commits per milestone; each commit builds and passes its own tests.
Branch: `feat/dataset-experiment-7`.
