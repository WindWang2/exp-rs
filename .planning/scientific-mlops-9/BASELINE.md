# BASELINE — Scientific MLOps / Reproducibility 9.0

Branch: `feat/scientific-mlops-9`
Worktree: `/home/kevin/projects/rs-studio/exp-rs-scientific-mlops-9`
Base: origin/master `132da5e998` (Merge PR #847 feat/geospatial-data-fabric-8)
Baseline captured: 2026-09-11

## Repository state at baseline

- `origin/master` = `132da5e998eca004d43285d1f71565f973030f5f`.
- Local `master` in the main worktree is **1 commit ahead** of origin/master:
  `8f6293bceb fix(core): resolve P0 defects ... (#848-#852)`. That commit and
  the follow-up #853–#882 remediation belong to the parallel
  `feat/execution-concurrency-lifecycle-9` track (worktree
  `exp-rs-exec-concurrency-9`); this track deliberately branches from
  **origin/master**, not local master, per goal §Git.
- **No open PRs** at baseline time.
- Open issues: #848–#882 (all `bug` + `ready-for-agent`, mixed
  critical/high/medium). Issue-by-issue triage: `ISSUE_TRIAGE.md`.
- Merged PRs #837–#847 = the 8.0 wave (processing, plugins, workbench,
  mlops, harness, execution, cartography, verification, model-runtime,
  geospatial-fabric). All `feat/*-8` remote branches are merged residue
  (behind master, ahead counts are merge-commit artifacts only).
- Historical residue: `feat/*-5/6/7` branches (merged via PR #818–#832),
  `itk-upstream/*` (upstream reference remote — unrelated).
- Active parallel 9.x worktrees at baseline:
  - `exp-rs-exec-concurrency-9` → `feat/execution-concurrency-lifecycle-9`
    (execution/concurrency/GUI defect wave #848–#882)
  - `exp-rs-geospatial-data-fabric-9` → `feat/geospatial-data-fabric-9`
  - `exp-rs-scientific-algorithms-9` → `feat/scientific-algorithms-9`
  - (uncommitted edits in the MAIN worktree touch `src/dataset/split.cpp`
    and other defect files — see `OVERLAP_MAP.md`; treated as
    execution-track remediation work, not authoritative for this branch.)

## Direct predecessor

PR #843 `feat/dataset-experiment-mlops-8` (planning dir
`.planning/dataset-experiment-mlops-8/`, merged). Its FINAL_REPORT declares:

- Delivered: execution→experiment bridge (truthful lifecycle, Interrupted
  reconciliation, step evidence), verified pins on recorded runs, MCP
  `run_workflow` recording args.
- **Documented 9.0-relevant follow-ups**:
  1. CLI auto-recording is NOT wired (`rs_pipeline_runner` bypasses the
     experiment monitor) — verified still true on this baseline.
  2. `resume_workflow` does not accept recording arguments
     (`WorkflowExperimentMonitor::optInResume` exists but no surface).
  3. Cross-thread resume-swap vs ghost-event race can record a ghost event
     that only the NEXT reconciliation reports.

## Environment (local verification baseline)

- Ninja + Release, GCC `/usr/sbin/c++`, Qt6, tests ON
  (`cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DENABLE_TESTS=ON`).
- Resource policy: builds `-j2` (max `-j4`), tests `-j1`/small parallel,
  one heavy build at a time (goal §0).
- CI is explicitly NOT a completion gate (goal §0); all completion evidence
  is local and reproducible from this worktree.
