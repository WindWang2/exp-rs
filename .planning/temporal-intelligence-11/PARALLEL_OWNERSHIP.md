# PARALLEL_OWNERSHIP — temporal-intelligence-11

Snapshot at track start (2026-09-15, origin/master = `a5b11b7f`). Re-verify before each rebase.

## Open PRs

| PR | Branch | Overlap with my primary scope | Policy |
|---|---|---|---|
| #1008 radiometric-spectral-workbench | `zcode/radiometric-spectral-workbench` | **None** in `*temporal*` files. Shared: `.gitignore`, `src/agent/CMakeLists.txt`, `src/analysis/CMakeLists.txt`, `src/app/CMakeLists.txt`, `src/core/CMakeLists.txt`, `tests/CMakeLists.txt` | Read-only w.r.t. its features; my CMake edits are append-only minimal blocks; expect additive conflicts only |

## Concurrent local (unpushed) branches — no open PR yet

| Branch | Temporal overlap | Shared files |
|---|---|---|
| `zcode/advanced-insar-platform-11` | none (SAR/InSAR) | `.gitignore` only |
| `zcode/cn-eo-product-physics-11` | none (product physics) | `.gitignore` only |
| `zcode/execution-runtime-convergence-11` | none (execution runtime) | `.gitignore` only |
| `zcode/spectral-intelligence-11` | none (superseded by PR #1008 lineage) | `.gitignore` only |

Policy: all of the above are **read-only** for me; I do not modify their worktrees, branches, or PRs. Shared integration files (`.gitignore`, `tests/CMakeLists.txt`, `src/processing/CMakeLists.txt`, `src/operators/CMakeLists.txt`, `data/agent/capabilities/*.json`, `docs/processing/temporal.md`, CHANGELOG) get minimal append-only edits committed in dedicated integration commits to keep rebase conflicts trivial.

## D18/D19 note

The prompt listed #991 (D18 workbench) / #992 (D19 foundry) as open with "D18 temporal mount files read-only" — **both merged before my worktree was created**, so master itself is the fact source. I read the D18 MissionContext mount surfaces on master and will not modify D18-owned files (`src/app/workbench/mission_*`, mission context/mount code). My UI work (package F) goes through `src/app/dialogs/temporal_analysis_dialog.*` and the agent spatial-tool registry — separate files, pre-existing seams.

## Issues

Open issues #1001–#1007: dataset/workflow/georef/io/agent — zero temporal scope. No dedupe action needed; none implemented by this track. `ISSUES.md` is a historical D3 backlog (T-1/T-2/T-3/C-2 all closed by Platform 10.0, confirmed in code); **not** treated as live backlog.
