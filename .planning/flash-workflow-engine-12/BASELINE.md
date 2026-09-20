# BASELINE — flash-workflow-engine-12

Captured 2026-09-20 ~11:20 (+08:00), after `git fetch origin --prune`.

## Master snapshot
- `origin/master` = `adf8f98952422fe9c386c56d64d5fb6a4a6642f1` ("docs(agents): record Platform 5.0 audit request")
- Prompt snapshot `2761a685` is 2 commits stale (fe7da0622 StepFun provider, adf8f9895 docs).
- Worktree HEAD pinned at `adf8f9895`.

## Open PRs (9) — none covers this track
| PR | Title | src/workflow overlap |
|----|-------|----------------------|
| 1124 | dev toolchain (worktree creator, build lock) | none |
| 1123 | fuzz/property lanes (parser/IPC/path/payload) | none |
| 1122 | Scientific Verification & Reproducibility 12.0 | none |
| 1121 | Workbench 12.0 | none |
| 1120 | offline lab platform 12.0 | pipeline_run_coordinator.cpp: `resumedDef`→`resumeDef` rename (GCC break workaround) |
| 1119 | Spectral Intelligence 12.0 | same file: `resumedDef`→`fullDef` |
| 1118 | EO model runtime 12.0 | same file: `resumedDef`→`fullDef` |
| 1117 | Data Foundation 12.0 | same file: `resumedDef`→`resumedDoc` |
| 1116 | Remote Geospatial Fabric 12.0 | none |

**Defect found:** master has a GCC compile error in `src/workflow/pipeline_run_coordinator.cpp`
(~line 717): `resumedDef` redeclared in same scope as an earlier `resumedDef`. All four
PRs carry a private rename workaround. This is our defect to fix canonically (owner scope).

## Issues
- Open: 0. Relevant closed: #1037, #1038, #1056, #1069 (workflow hardening wave) — all CLOSED.

## Historical branch audit: `origin/agent/flash-workflow-integrity`
8 unmerged commits vs master; diverged ~78 commits back. Per-file verdict:

| Change | Status on master |
|--------|------------------|
| Bounded checkpoint reads (kMaxCheckpointBytes) | PRESENT (workflow_checkpoint.cpp:30,139) |
| meta.ui JSON type-guard | PRESENT (workflow_definition.cpp:175-186) |
| IR2 artifact run-dir confinement | PRESENT (f4758f206) |
| resume/cancel dead-end fix | PRESENT (61a8c9b0d) |
| port wiring in lineage signatures | PRESENT (a5173d2f9) |
| marshalBlocking thread-affinity for coordinator accessors | ABSENT |
| resumed-artifact verification on resume | ABSENT |
| path_containment.h / workflow_limits.h shared headers | ABSENT |
| ir2_registry_node_executor hardening (+140 lines) | treat as requirements evidence |
| tests: run-state sync, checkpoint cache, checkpoint integrity, ir2 port-param | ABSENT |

Verdict: mostly superseded; remaining deltas are requirements evidence, re-implemented
in this track's design — no cherry-pick per track rules.

## Two workflow stacks (census finding)
- **D17 designer stack** (`WorkflowDocument`/`PipelineRunCoordinator`, ADR 0162, Qt6/QJson):
  Kahn frontier scheduler, ExecutionState enum, checkpoint_<runId>.json snapshots,
  CacheHit = lineage-signature match + artifact exists.
- **Engine 2.0 stack** (`WorkflowDefinition`/`WorkflowRun`/`WorkflowRunCoordinator`, jsoncpp):
  TaskCenter bridge; WorkflowRunState 10-state machine; StepPlan fingerprints,
  output identity (size/mtime/digest), operatorImplStamp; WorkflowCheckpointManager
  (atomic save, version=1, recoverInterruptedRuns, reconcileToInterrupted,
  electCheckpoints, archiveCompletedRun, run locks #727).
