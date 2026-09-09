# Professional Workbench & Unified UX Foundation 6.0

Branch: `feat/professional-workbench-ux-6` · Worktree: `../exp-rs-professional-workbench-ux-6` · Base: `master` @ `74fd0c7c`

## Mission

Turn the desktop shell into a coherent professional remote-sensing workbench: one
command surface, one lifecycle contract per workbench, bounded contextual
inspector, schema-driven parameter forms, unified task/result UX — with every
known P0/P1 lifecycle and concurrency defect fixed and pinned by tests.

## Hard constraints (from the goal brief)

- No work directly on `master`; at most **2 subagents** (reserved for Milestone L review).
- UI stays a thin client over TaskCenter → JobEngine → RSOperator; no raster
  kernels on the UI thread; no new scheduler.
- Use the vendored QGIS idioms; do not replace the rendering engine.
- Local validation resource-bounded: `-j2`, shared `vcpkg_installed`
  (`exp-rs-win/build-win`), offscreen Qt tests, no timing-sensitive assertions.
- No online CI dependency; push + open PR at the end, do not merge.

## Milestones

A. Eliminate P0 lifecycle crashes (#777 #778 #779 #780)
B. Rendering/view isolation (#793 + async render race test #796)
C. Concurrency/task-seam hardening (#797 #798 #799 #800)
D. Complete CommandRegistry migration (#792 #794 #795)
E. ContextRules 2.0 — deterministic unavailability reasons
F. Workbench lifecycle unification (#813)
G. Inspector 2.0 (#812 + contextual sections, lazy + cancellable)
H. SchemaFormBuilder 3.0 (conditional fields, units, diagnostics)
I. Unified task/result UX
J. Information architecture / design-token convergence
K. Accessibility & keyboard workflow
L. Adversarial review (2 subagents) + resolution

## Completion criteria

See the goal brief §9; tracked in TEST_MATRIX.md and FINAL_REPORT.md.
