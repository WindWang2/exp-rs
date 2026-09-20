# OWNERSHIP — ds41-build-portability (Track D2)

## Main owner (this track writes)

- `CMakeLists.txt` (root) — configure-time dependency diagnosis, feature-probe wiring.
  Shared with: every track that touches root CMake. Discipline: **append-only /
  minimal-diff integration**; never reorder or reformat unrelated blocks.
- `cmake/**` — new diagnostic + feature-probe modules (e.g. `cmake/SicnuDepDoctor.cmake`,
  `cmake/SicnuFeatureProbes.cmake`). Existing `Find*.cmake` untouched unless a proven
  portability defect is fixed minimally.
- `CMakePresets.json` — preset hygiene (append new presets; keep existing ones valid).
- `scripts/**` (generic build/setup scripts; `scripts/windows/**`) — the resource-safe
  build wrapper + dependency doctor launcher. Existing `_env.cmd` / `setup.cmd`
  semantics preserved (their -j2 hard bound stays authoritative on Windows).
- `docs/development/**` — NEW directory; this track creates and owns it.
- `tests/**` build-diagnostic tests (new files only; appended to `tests/CMakeLists.txt`).
- `.gitignore` — one appended `.planning` whitelist block for this track.
- `.planning/ds41-build-portability/**` — this track's planning notes.

## Read-only (consult, never edit)

- `src/**` — business algorithms. The only acceptable src edits are a *proven*
  portability include/API seam, minimal, and separately justified; none planned.
  **#1108 already landed the GDAL 3.8 idiom in `src/geospatial/io/*`; do not
  re-patch those files.**
- `src/geospatial/doctor/env_doctor.*`, `src/cli/cli_env_doctor.*`,
  `scripts/report_bundle_dependencies.py`, `scripts/windows/bundle_dependency_report.ps1`
  — the runtime Deployment-11.0 doctor. Boundary: **runtime checks are theirs,
  configure/build-time checks are ours.** New probes must not duplicate theirs;
  cross-reference from docs instead.
- `vcpkg.json` / `vcpkg-configuration.json` — dependency inventory changes would
  ripple every lane; out of scope.
- `.github/workflows/**` — explicitly out of scope (no new online CI).

## Shared append-only files & conflict protocol

- `CMakeLists.txt`, `CMakePresets.json`, `tests/CMakeLists.txt`, `.gitignore` are
  shared. If a parallel track modifies them, rebase onto latest `origin/master`
  and keep our hunks minimal and additive. Never copy another track's feature
  into this branch.

## Subagent rules

- No subagent cap, but subagents are limited to source audit, design, test design
  and review. **All compiles/ctest runs are coordinated serially by the main
  agent**; no two subagents may compile concurrently. Hard bound: `-j1` default,
  `-j2` maximum, `-j3+` forbidden; `ctest -j1`; `QT_QPA_PLATFORM=offscreen` for tests.
