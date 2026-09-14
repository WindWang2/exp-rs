# BASELINE — workflow-pipeline-designer (D17)

- Worktree: `/home/kevin/projects/rs-studio/exp-rs-workflow-pipeline-designer`
- Branch: `zcode/workflow-pipeline-designer`
- Base commit: `007e70cff6` — `fix(docs): arbitrate ADR 0146 collisions and
  complete .planning whitelist (#985)` (= `origin/master` at fetch time,
  2026-09-14).
- Base test state: not executed on the fresh worktree before the first
  change (build configured from scratch; see EVIDENCE.md §Commands).
  Master's CI-relevant workflow suites (`test_workflow_ir`,
  `test_workflow_repair`, `test_workflow_run_coordinator`, …) are upstream
  green per merge discipline of #985; D17 only *adds* targets and touches no
  existing TU, so the D17 risk surface is the new targets themselves.
- Host: Linux 6.18.49-2-lts x64, 16 logical cores / 62 GB (policy -j2),
  ccache available (`/usr/sbin/ccache`).
- Build: `cmake --preset dev-default` with the offline pybind11
  FETCHCONTENT override and ccache launchers (same recipe as the
  eo-ai-model-runtime-foundation-10 track, EVIDENCE command 4).
