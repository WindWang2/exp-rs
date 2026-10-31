# R25-D Baseline (实时侦察, 2026-10-28/29 UTC)

- Repository: https://github.com/WindWang2/exp-rs
- Base SHA: `2610cb4741e69a89124921e44c03e340e2522ee2` (origin/master, fetched 2026-10-28T17:03Z, unchanged from task-book snapshot)
- Open PRs: **0** (verified via `gh pr list --state open`)
- Open Issues: 144
- Remote branches: master + 8 leftover `agent/r23*`/`agent/r24*` branches — **all `ahead_by=0`** (fully merged; verified `git rev-list --left-right --count origin/master...origin/<b>` per branch)
- Local sibling worktrees (same host, not yet pushed, no remote PR collision at start):
  - `exp-rs-r25-A-20261028T140339Z` (science-processing)
  - `exp-rs-r25-B-20261028T140313Z` (data-model-runtime)
  - `exp-rs-r25-C-20261028T140339Z` (pi-mcp-execution)
  - `exp-rs-r25-E-20261028T170411Z` (teaching) — appeared ~1 min after this track
- This track worktree: `exp-rs-r25-D-20261028T170355Z`, branch `agent/r25-D-qt-qgis-cartography-20261028T170355Z`

## Build protocol

- Shared-host build lock: `flock $HOME/.cache/exp-rs-build.lock`; `CMAKE_BUILD_PARALLEL_LEVEL=2`; `CTEST_PARALLEL_LEVEL=1`; `QT_QPA_PLATFORM=offscreen`.
- Configure: `cmake -S . -B build-ci-fast -DCMAKE_BUILD_TYPE=Release -DENABLE_TESTS=ON -DENABLE_LOCAL_BUILD_SHORTCUTS=ON` (ci-fast preset equivalent, own build dir inside this worktree — no shared binaries).
- ccache transparent via `/usr/sbin/cc`,`/usr/sbin/c++` symlinks (cache 10 GB, hit rate ~48% at start).
- Host: 16 cores / 62 GB RAM, 5 r25 tracks building in parallel through the same lock.

## Key docs read

- README.md, CLAUDE.md, docs/repo-layout.md, docs/ui-architecture.md (702 lines — Ribbon/CommandRegistry/TaskCenter/SchemaFormBuilder/MapSpec-cartography chain, Parts I–VI through Workbench 10/11)
- CMakePresets.json (dev-default/offline-lab/ci-fast/ci-full/sanitizer-debug/release-package)

## Recent merged PRs consulted (from git log; bodies cross-checked on demand)

- #1716 r23-E platform/plugin/release, #1717 r23-D lab/grading/evidence, #1718 r24_a, #1719 r24_b, #1720 r24_c (ChartRegistry workspace_state, 布局菜单诚实化, surface snapshot), #1721 r23-C agent workbench, #1722 r23-A science algorithms, #1723 r23-B data/model runtime.

## Command evidence

- `git fetch origin --prune` → no output (up to date)
- `git rev-parse origin/master` → `2610cb4741e69a89124921e44c03e340e2522ee2`
- `gh pr list --state open --limit 100` → `[]`
- `gh api 'repos/WindWang2/exp-rs/branches?per_page=100'` → 9 branches (master + 8 merged leftovers)
