# ORACLES — ds41-build-portability (Track D2)

Every oracle below is objectively checkable. "Gate pass" = the stated command
exits 0 (or the stated assertion holds) on this host, captured in
`.planning/ds41-build-portability/EVIDENCE.md` and re-run twice before completion.
Host toolchain (VS2022 CMake 3.27.2, Ninja, vcpkg installed tree under the main
checkout's `build-dev/vcpkg_installed`) makes configure-level and hermetic
script-level gates runnable locally without network.

## O1 — Clean build dir configures without stale cache

**Statement.** From a directory that never held a CMake cache, configure of the
real project succeeds using only tracked inputs (presets + toolchain + vcpkg
installed tree reuse), i.e. `find_package`/presets do not depend on a stale
`CMakeCache.txt` from another directory.

**Check (run twice at the end):**
```
cd <worktree>
rm -rf .goal-loop-smoke
scripts/build.sh smoke --build-dir .goal-loop-smoke   # fresh dir; configure-only
```
Pass = exit 0, configure log contains the dependency summary block
(`=== SICNU dependency summary ===`), and no `CMakeCache.txt` was copied in.

## O2 — Missing/mismatched dependency gives an actionable diagnosis

**Statement.** A dependency that is absent or below minimum fails configure with
a message naming the package, the minimum, where it searched, and the concrete
fix per platform (apt/brew/pacman/vcpkg) — never a bare `find_package` error or
a deep link error. Version *mismatch* (present but too old) is likewise reported.

**Check:**
```
scripts/build.sh dep-fixture-test     # hermetic: configures the fixture project
```
The fixture (`tests/fixtures/dep_doctor_fixture/`) configures a project whose
`sicnu_require_dependency(...)` must fail with guidance. Pass = exit 0 for the
test driver AND captured output contains e.g. `libgdal-dev` / `vcpkg install gdal`
/ `GDAL minimum` strings; a negative control (dependency present) passes silently.
Additionally: configuring the real project with `-DQt6_DIR=/nonexistent` must
print the actionable Qt block (manual evidence run, recorded in EVIDENCE.md).

## O3 — Local build wrapper never exceeds the parallel cap

**Statement.** `scripts/build.sh` (POSIX) and `scripts/windows/build.cmd`
(Windows) compute jobs as: default 1, cap 2; any request above the cap is refused
with a clear message and nonzero exit; an inherited oversized
`CMAKE_BUILD_PARALLEL_LEVEL` is clamped; the underlying build command line never
carries `-j3` or higher.

**Check:**
```
scripts/build.sh selftest            # hermetic: no cmake/compiler needed
scripts/build.sh --jobs 8 ...        # must refuse (exit 2, message shows cap)
```
Pass = selftest exit 0 including cap and clamp assertions; `--jobs 8` exits 2.
On Windows, `scripts\windows\build.cmd selftest` and `--jobs 8` behave the same.

## O4 — Windows/POSIX path/space/Unicode script tests pass

**Statement.** The wrappers and their selftests run correctly when the repo path
and the invocation cwd contain spaces and non-ASCII characters (quoting defects
are the historical defect class here: F19 and #1088 both came from path handling).

**Check:** selftest creates fixture paths such as
`<tmp>/sü bü ild 路径/build portability` and invokes the wrapper logic against
them; asserts success and that no unquoted-expansion artifact (word splitting)
occurs. POSIX runs under bash on every platform; the cmd variant runs under
`cmd.exe` on Windows hosts only and is skipped (with a loud notice) elsewhere.
These run inside `selftest`, so O3 and O4 share one entry point.

## O5 — Feature probes are portable and covered by tests

**Statement.** `cmake/SicnuFeatureProbes.cmake` derives `SICNU_HAVE_*` cache
variables from compile checks (never from guessed version numbers), generates a
build-tree header, and a Catch2 test asserts the contract: every macro defined to
0/1, GDAL/Protobuf/OpenCV mode recorded, and the test fails on a tree without
the probes (i.e. it cannot pass vacuously on master).

**Check:**
```
cmake --build <build-dir> --target test_feature_probes_contract --parallel 1
ctest --test-dir <build-dir> -R test_feature_probes_contract -j1 --output-on-failure
```
Pass = build + test green (QT_QPA_PLATFORM=offscreen). Contract-only, so it is
valid on machines lacking optional deps (macros are 0 there, never undefined).

## O6 — Preset hygiene

**Statement.** `cmake --list-presets` works from a clean tree; presets cover
dev/test/offline(lab) profiles; every test preset pins `-j1` and
`QT_QPA_PLATFORM=offscreen`; every build preset pins its job cap; no preset needs
an existing cache to configure.

**Check:**
```
cmake --list-presets            # exit 0 from clean tree
python-based JSON assertions on CMakePresets.json (schema + caps + offscreen)
```
(the assertion driver is `scripts/build.sh preset-check`, exit 0).

## O7 — No-gate-weakening verification

**Statement.** No test was deleted, no assertion relaxed, no feature disabled to
produce green. New Catch2 tests must fail before this branch's fix: verified by
checking out the pristine `origin/master` state of the touched test list in a
scratch worktree and confirming the new tests fail there (compile error counts
as fail).

**Check:** recorded in EVIDENCE.md per test file.

## Resource budget for all gates

- Configure-only gates and hermetic selftests: unlimited rounds (cheap).
- Any compile: `--parallel 1` (max 2), only the affected target, never a full
  rebuild of QGIS/OTB/ITK. ctest: `-j1`, `QT_QPA_PLATFORM=offscreen`.
- All gate invocations log to `.goal-loop-logs/<gate>/` with a `SUMMARY.txt`.
