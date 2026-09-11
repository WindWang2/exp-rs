# BASELINE — audit of origin/master at execution time

Date: 2026-09-10. Master SHA: `322dfd3876c34ed62b42846598cacd711c8c91d6`
(identical to the planning snapshot; `git fetch --all --prune` found nothing
newer). Working tree clean at audit start.

## PR / issue state

- **Open PRs: none. Open issues: none.**
- Latest merges (all today): #835 (sdk namespace fix), #834 (GDAL range_cache
  bridge), #833 (CI restore), then the 7.0 platform wave #823–#832.
- Track-relevant merged PR: **#824 "feat(data): Dataset / Experiment /
  Reproducibility Platform 7.0"** — the direct predecessor of this track.
- #822 resolved issues #773–#817 (M1–M7), including several dataset/experiment
  store fixes (#774 deleteDataset commit, #786/787/788 split correctness,
  #789 bundle secret filter, #811 store TOCTOU).

## Surviving remote branches

`feat/cartography-platform-7`, `feat/cloud-geospatial-io-7`,
`feat/dataset-experiment-7`, `feat/execution-plane-runtime-7`,
`feat/model-runtime-multimodal-7`, `feat/pi-spatial-scientist-harness-7`,
`feat/plugin-isolation-runtime-5`, `feat/professional-workbench-7`,
`feat/scientific-algorithms-7`, plus older zcode/* branches — all correspond
to MERGED PRs (#823–#832, #761–#772). No divergent unmerged dataset/experiment
work exists; nothing to resurrect, nothing at risk of duplication.

## Prior-track planning read

`.planning/dataset-experiment-7/` (FINAL_REPORT + ARCHITECTURE + REVIEW_LOG):

- 7.0 delivered MCP surface (15 tools), SamplePromoter, ExperimentRunRecorder,
  fold audit, facets/quality cache, replay readiness, comparison extensions.
- **7.0's declared known limitations** (PR #824 description):
  1. CLI/MCP share verbs but not projection code (link-layering reason).
  2. **"ExperimentRunRecorder is the callable seam adapter; wiring it into
     every workflow state hook is follow-up integration work."** ← 8.0 WP-A.
  3. `get_tool_schema` doesn't resolve namespaced tools (schemas via
     tools/list includeSchemas).
  4. Near-duplicate detection relies on caller-supplied digests (no invented
     perceptual hashing) — a deliberate honesty contract, keep it.
  5. comparison_ext drops nested non-numeric subtrees silently (documented).

## Code state (verified against master)

- `src/dataset/` (~26 files): DatasetStore (SQLite WAL, schema v1, paged,
  forward-tolerant), draft→stage→commit→deprecate lifecycle,
  staleStagedDrafts, lineage edges, label schemas, samples, annotation
  revision chains, 12 split methods, 13-kind leakage auditor, fold audit,
  facets + quality cache, promotion with unmapped-value refusal.
- `src/experiment/` (~18 files): ExperimentStore (runs/experiments/metric
  records/lineage edges), ExperimentRunRecorder (Created/Running/Succeeded/
  Failed/Cancelled + reconcileStaleRuns; Interrupted exists in the state
  table but has no recorder API), replay readiness (ReproductionHooks),
  reproduction bundle (11-file bundle, checksums, secret filtering,
  Reference/Portable modes), unified lineage with tombstones, comparison
  extensions (protocol/schema compatibility, paired deltas with support
  gates).
- Execution plane: `WorkflowRunCoordinator` (singleton, checkpointed runs,
  resume, cross-process flock, ArtifactGC) emits
  `runStateChanged(runId, workflowId, state, startedMs, finishedMs)` with the
  coordinator mutex HELD (Qt signal; only queued receivers are safe).
  `WorkflowRun` aggregate carries definition, step plans (resolvedParams,
  output path/size/mtime/digest, errorMessage), artifacts map, terminal
  roll-up (Failed > Canceled > Completed).
- Governance mirror: `ProjectContext::openWorkspaceStore` connects the
  coordinator signal → `WorkspaceService::recordRun` (governance runs index).
  This is the ONLY production subscriber today.
- MCP `run_workflow` → `startTrackedPipelineJson` (no ProjectContext
  involved). CLI runner uses a headless ProjectContext + the same
  coordinator. GUI shell goes through `workflow_session_controller`.
- `ExperimentRunRecorder` production callers: **none** (grep over src/).
- CMake layering: `sicnu_dataset` → `Sicnu::data`; `sicnu_experiment` →
  `Sicnu::dataset` (layer guard: no GUI/network); `sicnu_workflow` (SHARED)
  → operators/runtime, no experiment edge (cycle-safe: nothing in the
  workflow stack links dataset/experiment).
- Test infra: Catch2; `sicnu_add_test` helper; light targets link
  `Sicnu::experiment Sicnu::dataset SQLite::SQLite3` only;
  `test_workflow_run_coordinator` links workflow+task_center+jobs and drives
  REAL tracked pipelines with fake executors (reusable E2E harness).
- Build env: Ninja Release build dir at `build/` (main tree), ccache warm
  (95k hits), 16 CPUs / 62 GB RAM. Worktree builds will configure a fresh
  build dir with ccache; targeted targets only.

## Overlap map vs. recent merges

| Recently merged | Relation to this track |
|---|---|
| #824 (dataset/experiment 7.0) | Direct predecessor — extend, do not rebuild. |
| #828 (execution plane 7.0) | Owns coordinator/worker/cache seams — bridge must be a pure consumer of `runStateChanged`. |
| #825 (model runtime 7.0) | Model identity pins read from model catalog via hooks; no runtime changes here. |
| #827 (pi harness 7.0) | Agent harness calls plan/workflow tools; MCP opt-in args must stay backward compatible. |
| #831 (verification 7.0) | Fault-injection probes live in sicnu_runtime; out of scope. |
| #822 (issues #773–#817) | Store fixes already landed; tests already assert them. |

## Conclusions

1. The prompt's "primary gap is integration completeness" is exactly right:
   WP-A (automatic execution→experiment lifecycle wiring) is the verified
   headline gap.
2. Work packages B–I are largely Implemented/Partial in master; deepening
   must be verified gap-by-gap (see CAPABILITY_MATRIX.md) — the instruction
   "when master already contains a better implementation, do not duplicate
   it" applies to most of C/D/E/F/G/H.
3. Cross-track seams to keep minimal: `workflow_run_coordinator.{h,cpp}`
   (execution-plane track #828 owns it), `mcp_server.cpp` (agent track),
   `tests/CMakeLists.txt`, root `CMakeLists.txt`.
