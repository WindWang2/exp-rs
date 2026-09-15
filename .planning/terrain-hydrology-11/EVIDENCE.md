# EVIDENCE — terrain-hydrology-11

Append-only. Every capability claim maps to a command + exit code run in this
worktree, or is marked not-executed with the blocker.

## Phase 0 (2026-09-15)

- `git fetch origin --prune` → exit 0; origin/master = `a5b11b7f10fa010c1c060864fb427d777ba9a4aa`.
- `gh pr list` → 1 open PR (#1008 spectral/radiometric; CONFLICTING vs master).
  `gh pr view 1008` + `gh pr diff 1008 --name-only` → no terrain file overlap
  (see PARALLEL_OWNERSHIP.md).
- `gh issue list` → #1001–#1007, all io/workflow/dataset/georef → OUT_OF_SCOPE for
  terrain track; no dedupe against any planned deliverable.
- ISSUES.md read (lines 1–60): stale D3 operator-gap backlog (temporal/SAR/HSI/
  cartography); none terrain → not implemented.
- Code audit citations: see BASELINE.md (terrain_flow.h contracts;
  rs_terrain_flow_operator.cpp:70 documented flat-resolution debt).
- `git worktree add ../exp-rs-terrain-hydrology-11 -b zcode/terrain-hydrology-11 origin/master` → exit 0.
- `git check-ignore -v .planning/terrain-hydrology-11/GOAL.md` → matched
  `.gitignore:119:.planning/*` before fix; after whitelist append
  (`!.planning/terrain-hydrology-11/**`) the path is no longer ignored
  (verified via `git status` visibility of the planning files).
- OUT_OF_SCOPE findings: none yet.
