# OVERLAP_MAP — model-runtime-multimodal-9

Parallel 9.0 worktrees observed (2026-09-11):

| Worktree | Branch | Overlap with this track |
|---|---|---|
| `exp-rs-exec-concurrency-9` | feat/execution-concurrency-lifecycle-9 @ 8f6293bceb | TaskCenter/Workflow concurrency — scheduler seam; we only CALL the resource seam, no shared file edits expected |
| `exp-rs-geospatial-data-fabric-9` | feat/geospatial-data-fabric-9 @ 132da5e998 | `src/geospatial/**` — we consume `RasterReader`/`raster_convert`, never edit |
| `exp-rs-scientific-algorithms-9` | feat/scientific-algorithms-9 @ 132da5e998 | `src/processing/algorithms` — disjoint |
| `exp-rs-scientific-mlops-9` | feat/scientific-mlops-9 @ 132da5e998 | ExperimentStore/MLOps — the evidence seam consumer; keep seam types stable and additive |
| MAIN worktree (local master) | uncommitted #848–#882 remediation | **Touches `rs_inference_operator.cpp` (#872)** and other operators files. Expect textual conflict at sync; resolution documented in ISSUE_TRIAGE.md (keep both; disjoint regions). It also touches `task_center.cpp`, `workflow_run_coordinator.*` — not mine. |

## Shared-file policy

- Root `CMakeLists.txt`, `tests/CMakeLists.txt`, `CHANGELOG.md`,
  `.gitignore`: minimal additive edits, made at the milestone that needs
  them, re-synced with `origin/master` before every push.
- No edits into `src/app/**`, `src/workflow/**`,
  `src/processing/framework/**`, `src/geospatial/**`, `src/data/**`,
  `src/dataset/**`, `src/experiment/**`.
- If another branch starts rewriting a seam I depend on
  (`model_execution_service`, `model_runtime.*`), I integrate through the
  existing stable interface and re-verify; no bilateral rewrite.
