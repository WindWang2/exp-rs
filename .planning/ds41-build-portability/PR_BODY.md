## ds41-build-portability (Track D2) — cross-platform build, dependency doctor & local gate

**Baseline:** `origin/master` @ `adf8f979` ("docs(agents): record Platform 5.0 audit request"), fetched 2026-09-20; 0 open PRs / 0 open issues at pre-read time; 13 stale remote branches judged residue (their build-scope deltas are the already-merged #1108/#1111 content).

**Live dedup result:** #1099/#1114 closed-unmerged (superseded); **#1108 (merged)** landed the Protobuf CONFIG→MODULE fallback and the GDAL 3.8 quiet-error idiom — this branch does not re-patch those; **#1019 (merged)** landed the *runtime* env-doctor — this branch's doctor is the *configure-time* complement (different phase, no duplicated probes). No open PR covers any subtask of this track.

## What this adds

1. **Dependency doctor** (`cmake/SicnuDepDoctor.cmake`, wired into the root `CMakeLists.txt`): required dependencies that are missing/too old now fail with an actionable diagnosis (package, minimum, where searched, exact install command per platform — apt/pacman/dnf/brew/vcpkg), and every successful configure ends with a paste-able dependency summary (versions, discovery mode, resolved paths, vcpkg triplet, OTB/NetCDF rows).
2. **Preset hygiene** (`CMakePresets.json`, `scripts/check_presets.py`): new `offline-lab` preset (zero-egress teaching profile), build presets pin jobs within the project cap, test presets pin `CTEST_PARALLEL_LEVEL=1` + `QT_QPA_PLATFORM=offscreen`; `scripts/build.sh preset-check` asserts all of it.
3. **Portable feature detection** (`cmake/SicnuFeatureProbes.cmake`, `tests/test_feature_probes.cpp`): compile-check probes (never version guesses — the #1108 lesson) for the GDAL quiet-error APIs, `std::format`, `std::stacktrace`, Qt6 QtTypes; results land in a generated header where every macro is always 0/1, and a Catch2 contract test pins it (static_asserts + runtime cross-checks).
4. **Resource-safe local build wrappers**: `scripts/build.sh` (POSIX) and `scripts\windows\build.cmd` (Windows): one entry point with default `-j1`, hard cap `-j2` (higher refused with exit 2), inherited parallelism env clamped, `ctest -j1` + offscreen, per-run logs with failure `SUMMARY.txt`, `doctor`/`configure`/`build`/`test`/`smoke`/`selftest`/`preset-check`/`dep-fixture-test` subcommands.
5. **Clean-tree verification**: `smoke` configures from an empty build dir (stale cache removed first), seeding only toolchain *locations* from an existing tree via a CMake `-C` initial-cache script (offline, no vcpkg bootstrap), reuses the vcpkg installed tree read-only, and asserts the dependency summary block exists.
6. **Hermetic gates** (no compiler/Qt/network): `selftest` (cap matrix + path/space/Unicode quoting from a fixture dir), `dep-fixture-test` (missing→install guidance, stale→minimum-version diagnosis, ok→summary, scope→wrapped call behaves like the direct call).
7. **Docs**: new `docs/development/` (build-from-scratch per OS, resource discipline, dependency doctor).

## Design notes

- The require/probe wrappers are **macros, not functions** — load-bearing: vcpkg's `find_package` override is a macro and repo find modules (e.g. `FindSqlite3.cmake`) enforce run-once through directory-scope variables; a function wrapper breaks direct-call equivalence (this exact failure was hit and fixed during the track; the fixture pins it with a vcpkg-style `<Pkg>_FOUND` cache reset).
- #1108's landed Protobuf/GDAL handling is behaviour-identical (only a mode variable + a note added); `src/**` is untouched (verified: `git diff --name-only origin/master...HEAD -- src/` is empty).

## Verification (all on the reference host: VS2022 14.38, CMake 3.27.2, Ninja, Qt 6.8.0, GDAL 3.12.4, vcpkg x64-windows)

| gate | command | result |
|---|---|---|
| hermetic selftest (POSIX) | `scripts/build.sh selftest` | ALL PASS (twice) |
| hermetic selftest (Windows) | `scripts\windows\build.cmd selftest` | ALL PASS (twice) |
| dependency-doctor fixture | `scripts/build.sh dep-fixture-test` | 5/5 ALL PASS (twice) |
| preset hygiene | `scripts/build.sh preset-check` | ALL PASS (twice) |
| clean-tree configure (O1/O2) | `scripts\windows\build.cmd smoke --base-cache <cache> --catch2-source C:\deps\catch2-src` | exit 0, summary present, **two consecutive passes** (also two pre-review passes) |
| feature probes (real configure) | configure log | GDAL ctor=1, pusher=1, std::format=1, QtTypes=1, stacktrace=0 (MSVC needs /std:c++latest) |
| contract test | `ninja test_feature_probes` + run | builds; **9 assertions in 4 cases pass** (twice) |
| potency (red-before) | compile the test without the generated header | fatal C1083 (fails on a tree without the module) |
| job cap | `build --jobs 3` / `--jobs 8` | refused, exit 2, cap message |
| `cmake --list-presets` | real CMake parser | accepts all six configure presets |

Resource discipline observed throughout: `-j1` default / `-j2` maximum (compiles via `CMAKE_BUILD_PARALLEL_LEVEL`), `ctest -j1`, `QT_QPA_PLATFORM=offscreen`; no full-tree rebuilds beyond the configure gates and one targeted test build.

## Independent review

A separate read-only reviewer audited `origin/master...HEAD`: **0 P0, 5 P1, 6 P2, 5 P3**. All P1 and the cheap P2/P3 are fixed (final commits); dispositions:
- P1-1 Windows lane default was 2 → both lanes default to 1 (inherited env only clamps);
- P1-2 `;`/`|` in a probe description corrupted records/header → now rejected loudly;
- P1-3 smoke only echoed the summary → now asserts it (both lanes);
- P1-4 the scope case was not potent (CMake's cached `<Pkg>_FOUND` short-circuited it) → fixture now clears only the cache entry, mirroring vcpkg's backup/restore; knocked out both ways (function variant reproduces the original FATAL, macro variant passes);
- P1-5 clamped exports died in a command substitution → clamp now runs in the current shell;
- P2-6/P2-7/P2-8/P2-9/P2-10/P2-11, P3-12/15/16 fixed as noted in the commits.

Post-fix re-verification: all hermetic gates pass twice; clean-tree smoke passes twice; contract test passes twice.

## Known limitations

- The clean-tree smoke reuses the *installed* vcpkg tree from a reference build (`--base-cache`) and a local Catch2 clone (`--catch2-source`) — by design the gate never downloads anything; a first-ever machine configure follows `docs/development/build-from-scratch.md`.
- `std::stacktrace` is honestly reported unavailable on MSVC without `/std:c++latest` (probe truth, not a defect).
- The `.goal-loop-ledger.md` is committed (repo convention — other tracks do the same); planning notes live under `.planning/ds41-build-portability/`.
- No online CI is added or required; all evidence above is local.

## Conflict hotspots

- Root `CMakeLists.txt` (additive: 20 `find_package(… REQUIRED)` → `sicnu_require_dependency`, a mode variable, notes, the probes block, the summary call), `CMakePresets.json` (additive fields + one preset), `tests/CMakeLists.txt` (one added line), `.gitignore` (one appended whitelist block). If a parallel track touches these, rebase and keep the hunks additive.
