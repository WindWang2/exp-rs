# R22-07 (r22-infra) Baseline — 2026-10-25

## Repository facts (live-verified at start)

- `origin/master` = `911e0b17a3533ca0d79c1b68b000d6052c4c3c61` (matches task snapshot; local master clean & identical).
- `gh repo view`: default branch `master`, viewer permission ADMIN.
- Remote branches: `agent/r20-01..05-*` (merged via #1668–#1672), `agent/r21-{a..e}-*` (open PRs #1673–#1677), `master`.
- Open PRs (all R21, none merged at start): #1673 (B/data), #1674 (E/teaching-verify), #1675 (C/pi-mcp), #1676 (A/scientific), #1677 (D/qt-qgis).
- Open issues triaged for this track: #1696 #1697 #1698 #1681 #1682 #1699 #1683 — **all OPEN_UNFIXED on master at start** (source-verified, see ISSUE_TRIAGE.md; not taken from issue text alone).

## Worktree / branch

- Branch: `agent/r22-infra-20261025T133318Z` from `origin/master`.
- Worktree: `/home/kevin/projects/rs-studio/exp-rs-r22-infra-20261025T133318Z` (single worktree for this track; no other agent's worktree touched).
- Build dir: `build-r22-infra` inside the worktree; shared seven-track lock `/tmp/exp-rs-r21-build.lock` used for every build.

## Host environment (verification context)

- Linux x86_64, 16 cores, 62 GiB RAM (~49 GiB avail during work), CachyOS, `/bin/sh -> bash`.
- Qt 6.12.0, GDAL 3.13.3, PROJ/GEOS/QGIS system installs (found by configure).
- GCC 16.2.1 — **Debug builds of QGIS-scale TUs hit deterministic dwarf2out/front-end ICEs on this host** (`internal compiler error: in output_die / verify_marks_clear / Segmentation fault` at `qgsrunprocess.cpp:302`, `external/lazperf/charbuf.cpp:121`, `qgsmaplayer.h:1405`, …), surviving the repo's `cmake/raise-compiler-stack.sh` 12-attempt retry. A prior Debug build (`main/build-dev/sicnu_geo_rs_cli`, Oct 19) predates the toolchain regression. **Local verification therefore uses a Release configure** (`CMAKE_BUILD_TYPE=Release`, CI tier1's shape, no `-g` → no dwarf2out path). Recorded as host environment fact, not a product regression; CI remains authoritative for Debug/MSVC/clang builds.
- Local clang-tidy 21.x: nondeterministic SIGSEGV on Qt-heavy TUs (observed both on pristine master files and edited files) plus `unknown argument: '-mno-direct-extern-access'` (GCC-only flag in compile_commands) — local lint runs are advisory; CI lane is authoritative. Evidence in TEST_RESULTS.md.

## Configure command (Release)

```
cmake -B build-r22-infra -S . -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DENABLE_TESTS=ON -DSICNU_TOLERANT_TEST_DISCOVERY=ON
```
(ccache launchers were dropped after the first attempt: they defeated the repo's
GCC-16 stack-raise launcher injection — root CMakeLists sets it only when no
launcher is present — and the ICEs then reproduced immediately.)
