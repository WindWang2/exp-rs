# BASELINE — ds41-build-portability (Track D2)

Captured: 2026-09-20 (end of Phase 0 pre-read). All facts below were re-derived
live from `origin` at this timestamp; nothing is copied from the task prompt.

## Live master

- `origin/master` = `adf8f98952422fe9c386c56d64d5fb6a4a6642f1`
  ("docs(agents): record Platform 5.0 audit request"). Identical to the task
  prompt's snapshot SHA; `git fetch origin --prune` produced no master drift.
- Worktree `agent/ds41-build-portability` created from that SHA.

## PR state (live)

- `gh pr list --state open` → **0 open PRs** (matches prompt snapshot).
- Recent merged PRs relevant to this track:
  - **#1108 (MERGED 2026-09-19T15:11Z)** — "fix(ci): unblock master configure/build
    (Protobuf MODULE + GDAL 3.8 quiet errors)". Changes: `CMakeLists.txt` prefers
    Protobuf CONFIG then falls back to MODULE; `src/geospatial/io/{stage_ledger,
    vector_interchange,subdataset_inventory,metadata_patch}.cpp` replace
    `CPLErrorStateBackuper(CPLQuietErrorHandler)` (GDAL 3.9+ only) with
    `CPLErrorStateBackuper` + `CPLErrorHandlerPusher(CPLQuietErrorHandler)`
    (works on GDAL 3.8.4). **This is the expected overlap hotspot; its landed
    pattern is a hard constraint on this track** (do not re-patch the same files).
  - **#1019 (MERGED 2026-09-15)** — Deployment 11.0: landed the **runtime**
    env-doctor (`src/geospatial/doctor/env_doctor.*`, `src/cli/cli_env_doctor.*`,
    `tests/test_env_doctor.cpp`, `scripts/report_bundle_dependencies.py`,
    `scripts/windows/bundle_dependency_report.ps1`, `docs/deployment/env-doctor.md`).
    Runtime-only (deployed bundle / GDAL drivers / proj.db); it deliberately does
    not probe a developer toolchain. This track's Dependency Doctor is the
    **configure-time** complement; overlap must be avoided, not duplicated.
  - #1115/#1113/#1112/#1111/#1110 — fail-closed wave-2 fixes, business code, out of scope.
- **#1099** and **#1114** (both titled "fix(ci): portable Protobuf resolution ...")
  are **CLOSED, not merged** — superseded by #1108.

## Issue state (live)

- `gh issue list --state open` → **0 open issues**.
- Closed build-portability residue confirmed already fixed on master:
  #1055 (MSVC `getpid` in range_cache, fixed by #1058), #997 (missing windows.h/unistd.h,
  fixed in #1030), #961 (bare `setenv` on MSVC, #983), #999 (e2e target missing Qt6::Xml,
  #1000), #993 (macOS/Windows compile errors), #1088 (sample launcher `--out=` contract).

## Remote branches (dedup判定)

All 13 remaining `origin/agent/*` and `origin/fix/*` branches are **diverged
historical residue** (behind master by 80, ahead 1–12 commits). None carries an
unmerged increment inside this track's owner scope; the two build-touching ones
(`fix/ci-master-unblock`, `fix/r2-ci-protobuf-multimode`, identical +13/-1
`CMakeLists.txt`) are the Protobuf CONFIG→MODULE fallback already landed via
#1108, and `fix/review-issues-1033-1056`'s `scripts/gen_samples.*` delta landed
via #1111 (#1088). Details in `DEDUP.md`. No cherry-picks.

## Local host build capability (evidence for local gates)

- VS 2022 Community (14.38.33130), CMake **3.27.2-msvc1** (VS-bundled),
  Ninja at `C:\Qt\Tools\Ninja\ninja.exe`.
- Qt 6.8.0 `C:\deps\Qt\6.8.0\msvc2022_64`; vcpkg `C:\deps\vcpkg` (manifest mode,
  baseline `7f3781e1…`); QCA/QScintilla/Qt6Keychain/Qt-keychain builds under `C:\deps`.
- Main checkout has a working `build-dev/` (Debug, Ninja, vcpkg manifest mode,
  `VCPKG_INSTALLED_DIR=<repo>/build-dev/vcpkg_installed`) — reusable as a
  dependency source for clean-tree configure smoke tests via
  `-DVCPKG_MANIFEST_MODE=OFF -DVCPKG_INSTALLED_DIR=…` (no network needed).
- Git Bash PATH has no cmake/ninja/g++; the VS-bundled cmake path must be used explicitly.

## Track ledger

See `.goal-loop-ledger.md` (worktree-local, not committed unless repo policy changes).
