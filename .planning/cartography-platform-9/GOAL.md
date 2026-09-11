# GOAL — Intelligent Cartography / MapSpec / Template Platform 9.0

Repo `WindWang2/exp-rs`, branch `feat/cartography-platform-9` from
`origin/master` @ `f316dfdbb4` (post-8.0-series, includes the #848–#852 P0
and #853–#882 P1/P2 remediation commits). Worktree
`/home/kevin/projects/rs-studio/exp-rs-cartography-platform-9`.

Development scale: program-level (≥ 3×10^8 tokens budget), not surface
patches. Max 2 read-only review subagents. Online CI/CD is NOT a completion
condition — all evidence is local and reproducible.

## Direction

On top of 7.0 (constraint solver explainability, typography engine) and 8.0
(NoData renderer, locator connector, MapSpec v5, page-aware solver, compose
identity), build **Cartography Platform 9.0**:

- reliable constraint solving (M0/M1),
- professional multi-page/atlas composition (M2),
- componentized templates (M3/M4),
- thematic maps / charts / tables (M5/M6),
- typography 3.0 (M7),
- map QA + converging auto-repair (M8),
- reproducible export (M9),
- harness typed-tool integration (M10).

## Ownership

Owned: `src/agent/mapspec/**`, `src/agent/cartography/**`, map
templates/components/styles under `data/cartography/**`, cartography-specific
LayoutService types, map QA/compose/export contracts, visual regression
tests/docs under `docs/cartography/**`, cartography tests under `tests/`.

NOT owned: generic Workbench shell, QGIS rendering engine (call it, never
replace it), Pi agent loop, execution scheduler (WorkflowRunCoordinator →
TaskCenter → JobEngine → Executor), `src/geospatial` (I/O authority),
dataset/experiment stores.

## Historical issue leads (all re-verified CLOSED on this base)

- #864 relaxation oscillation → fixed by `restoreRects(mPreSolveRects)` on
  non-convergence + min/max clamp on match_* targets (commit `f316dfdbb4`).
- #865 `fit_content` zero-size rect permanently blocked → fixed by leader
  rect fallback from declared `rect_mm` (same commit).
- #866 `has(path)` operand → fixed by `Op::Has` handler in `operandValue`.
- #877 NaN compare semantics → fixed by `isnan → op == "!="` in
  `compareValues`.
- #867 recipe degradation → harness-side (`recipe_catalog.cpp`); Harness
  Track owns it; this track does not touch it.

M0 re-verifies these with regression corpus entries; new defects found in
the same areas are fixed on the new root cause.

## Milestones

M0 solver/expression correctness corpus · M1 constraint solver 9.0 ·
M2 multi-page/atlas · M3 component system audit+growth · M4 template
composition · M5 thematic cartography · M6 charts/tables · M7 typography 3.0
· M8 map QA/auto-repair · M9 export/reproducibility · M10 harness typed
tools. See MILESTONES.md for the per-milestone scope decisions.
