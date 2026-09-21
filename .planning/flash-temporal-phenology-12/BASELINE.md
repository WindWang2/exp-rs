# BASELINE — flash-temporal-phenology-12 (Track 09, Temporal Phenology & Irregular Time-Series Analytics 12.0)

- Worktree: `C:\Users\wangj.KEVIN\projects\exp-rs-worktrees\flash-temporal-phenology-12`
- Branch: `agent/flash-temporal-phenology-12`
- Base commit: `adf8f98952422fe9c386c56d64d5fb6a4a6642f1` — `docs(agents): record
  Platform 5.0 audit request` (= `origin/master` at fetch time, 2026-09-20 ~00:55 +08:00).
  2 commits newer than the prompt snapshot `2761a6857`: `fe7da0622` (StepFun
  preset provider profile) + `adf8f9895` (docs).
- Host: Windows (Devin Desktop), Git Bash. github.com:443 unreachable directly;
  api.github.com reachable; all git/gh traffic via system proxy `127.0.0.1:7890`
  (`HTTPS_PROXY`/`HTTP_PROXY` env vars only — no git config changes).
- Build/test policy: `cmake --preset build-dev`, `cmake --build build-dev -j1`
  (max -j2), `ctest -R <family> -j1` with `QT_QPA_PLATFORM=offscreen`.
- Incident: the worktree branch ref `agent/flash-temporal-phenology-12` was
  deleted out from under the worktree shortly after creation (concurrent git op
  suspected; `HEAD` went unborn). Restored via `git update-ref` to adf8f9895 —
  files/index untouched. If it recurs, re-run the same update-ref.

## Live PR/issue state at fetch time

Open PRs (5) — all sibling `flash-*-12` tracks, none temporal:

| PR | Branch | Overlap with this track |
|----|--------|-------------------------|
| #1116 | agent/flash-geospatial-fabric-12 | Shared: `tests/CMakeLists.txt`. Domain `src/geospatial/{fabric,remote,stac}` — read-only for me |
| #1117 | agent/flash-data-experiment-12 | Shared: `tests/CMakeLists.txt`, `pipeline_run_coordinator.cpp` (won't touch) |
| #1118 | agent/flash-model-runtime-12 | Shared: `src/operators/CMakeLists.txt`, `tests/CMakeLists.txt` |
| #1119 | agent/flash-hyperspectral-12 | Shared: `src/operators/CMakeLists.txt`, `src/processing/CMakeLists.txt`, `tests/CMakeLists.txt`, `rs_operators_init.cpp`, `determinism_census.snap.json`, **4 temporal algorithm_meta sidecars** — treat those files as theirs |
| #1120 | agent/flash-offline-labs-12 | Shared: `tests/CMakeLists.txt`, `.gitignore` whitelist convention |

Open issues: **0**.

## Prior landed temporal work (do-not-rebuild)

- #973 (10.0): regular calendars, joint harmonic-break change model, multi-ROI
  extraction, ML feature tables.
- #986 (D16): regularized cube, phenology, BFAST, trends, STARFM, timeline
  widgets, agent tools, Lab08.
- #1014 (11.0): seasonal break attribution, model selection, uncertainty,
  phenology 2.0.
- #1113: `rs:temporal_index_series` EVI/SAVI scale-probe stale-buffer fix
  (#1076) + `parseIsoDate` hardening — do not regress.

## Remote branch residue

`agent/ds41-*`, `agent/flash-*-integrity`, `agent/glm53-*`, `fix/*` — historical
tracks superseded by merged wave #1100–#1115; read-only evidence, never a
baseline. No `*temporal*` branch existed; name was free.

## Base test state

Not yet executed (fresh worktree). Temporal targeted suites to be identified
from `tests/CMakeLists.txt` via census; targeted-first per policy.
