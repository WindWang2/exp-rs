# PARALLEL_OWNERSHIP — qgis-editing-annotation-11

Audited at track start (2026-09-15), after `git fetch origin --prune`.

## Open PRs / remote branches

| Ref | Business files (from `gh pr diff --name-only`) | Overlap with this track | Policy |
| --- | --- | --- | --- |
| **#1008** `zcode/radiometric-spectral-workbench` (open, CONFLICTING, not draft) | `.gitignore`; `.planning/radiometric-spectral-workbench/*`; `docs/adr/0158-*`; `src/agent/spatial_tools/spectral_spatial_tools.*`, `src/agent/spatial_tools/spatial_tool.cpp`, `src/agent/CMakeLists.txt`; `src/analysis/atmospheric/*`, `src/analysis/hyperspectral/continuum_removal.*`, `src/analysis/CMakeLists.txt`; `src/core/radiometric_state.*`, `src/core/spectral_library.*`, `src/core/CMakeLists.txt`; `src/app/widgets/{band_composite_palette,spectral_profile_widget}.*`, `src/app/CMakeLists.txt`; `src/processing/algorithms/{radiometric_calibration,spectral_indices,spectral_unmixing}.*`; `tests/CMakeLists.txt`, `tests/test_*.cpp` (spectral set) | Shared integration files only: `.gitignore`, `tests/CMakeLists.txt`, `src/app/CMakeLists.txt`, `src/analysis/CMakeLists.txt`, `src/agent/CMakeLists.txt` | Its business files **read-only** for us (we never touch spectral files). For shared integration files: append-only minimal diffs at distinct locations (new test blocks, new source list lines, new `.gitignore` whitelist lines). Rebase expects trivial or no conflicts; if conflicts appear, resolve by keeping both appends, never ours/theirs. |
| Remote branches | only `origin/zcode/radiometric-spectral-workbench` (= #1008 head) | — | same as #1008 |

## Merged-since-snapshot (prompt snapshot stale)

- #991 D18 unified-mission-workbench → merged; `src/app/workbench/**` + MissionContext now master authority. We **read** `mission_context.h` patterns but keep our integration diff into `main_window_workbench.cpp` minimal-append.
- #992 D19 dataset-foundry → merged; `src/dataset/**`, experiment/benchmark internals read-only for us (issues #1003/#1004/#1007 live there; out of scope).

## Open issues dedupe

#1001–#1007: all out of this track's primary scope (domains: io, workflow, dataset, agent-sample-tools, georef GCP). Evidence + dispositions in `BASELINE.md`. None will be implemented or closed by this track. Lesson carried in-scope: fail-closed CRS transforms (issue #1005 pattern) applied to our ROI CRS path.

## This track's exclusive write scope (as executed)

- `src/app/editing/**` (new)
- `tests/test_edit_*.cpp`, `tests/test_editing_e2e.cpp` (new)
- `docs/workbench/editing-platform.md`, `docs/adr/0163-editing-session-authority.md` (new)
- `.planning/qgis-editing-annotation-11/**` (new)
- Shared integration (append-only): `src/app/CMakeLists.txt`, `tests/CMakeLists.txt`, `.gitignore`, `CHANGELOG.md`
- Minimal wiring: `src/app/main_window_workbench.cpp` (session+agent tool mount, append-only)

## Forbidden for this track

- vendored `src/gui/**`, `src/core/**`, `src/analysis/**` modifications
- `src/app/main_window_*` beyond the single append wiring point
- #1008 business files; D19 dataset internals; other tracks' worktrees
