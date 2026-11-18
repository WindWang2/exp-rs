# R33-T06 — RESUME

Every new shell must restore these explicitly; nothing is inherited.

```bash
export EXPRS_TRACK=t06-desktop-workflows
export EXPRS_REPO=/home/kevin/projects/rs-studio/main                  # read-only origin checkout
export EXPRS_WT=/home/kevin/projects/rs-studio/exp-rs-r33-t06-desktop-workflows-20261117T141119Z
export EXPRS_BRANCH=zcode/r33-t06-desktop-workflows-20261117T141119Z
export EXPRS_BASE_BRANCH=master
export EXPRS_BASE_SHA=5ff15400047a6c55892669c9e9911e0e3b51bd04
export EXPRS_BUILD="$EXPRS_WT/build-dev"
export EXPRS_CONFIG=Debug
export EXPRS_PLAN="$EXPRS_WT/.planning/r33-t06-desktop-workflows"
export EXPRS_HOST_LOCK="$HOME/.cache/exp-rs-agent/host-build"
export CMAKE_BUILD_PARALLEL_LEVEL=4
export CTEST_PARALLEL_LEVEL=1
export OMP_NUM_THREADS=1 OPENBLAS_NUM_THREADS=1 MKL_NUM_THREADS=1
# MANDATORY on this host: /tmp is a 10 MB tmpfs, and g++ writes its assembler
# temp file there — the build dies with
#   "fatal error: error writing to /tmp/ccXXXX.s: No space left on device".
export TMPDIR="$HOME/.cache/exp-rs-agent/tmp" TMP="$TMPDIR" TEMP="$TMPDIR"
mkdir -p "$TMPDIR"
```

## Configure (must be repeated verbatim)

```bash
cd "$EXPRS_WT"
python3 scripts/dev/resource_guard.py --lock-dir "$EXPRS_HOST_LOCK" --parallel 4 --max-parallel 4 --wait 240 -- \
  python3 scripts/dev/resource_guard.py --lock-dir "$EXPRS_BUILD" --parallel 4 --max-parallel 4 --wait 240 -- \
  cmake --preset dev-default \
    -DOpenCV_DIR=/home/kevin/projects/rs-studio/main/.scratch/ui-build-deps/opencv/usr/lib/cmake/opencv5
```

Without `-DOpenCV_DIR` the configure dies at
`src/processing/CMakeLists.txt:12`. Full generation takes ~26 min on this host.

## Build the targets this track touches

```bash
python3 scripts/dev/resource_guard.py --lock-dir "$EXPRS_HOST_LOCK" --parallel 4 --max-parallel 4 --wait 240 -- \
  python3 scripts/dev/resource_guard.py --lock-dir "$EXPRS_BUILD" --parallel 4 --max-parallel 4 --wait 240 -- \
  cmake --build "$EXPRS_BUILD" --config Debug --parallel 4 --target \
    test_r33_t06_preview_identity test_asset_preview_service
```

## Test

```bash
QT_QPA_PLATFORM=offscreen ctest --test-dir "$EXPRS_BUILD" -C Debug -j1 \
  -R 'r33_t06_preview|test_asset_preview_service' --output-on-failure
```

`sicnu_discover_tests` registers one CTest case per Catch2 `TEST_CASE`, so use
`--show-only=json-v1` to see the real names; a target name is not a case name.

## Locks / activity at the last checkpoint

* `host-build.agent-build.lock` is released between commands (the guard owns it
  only for the payload).
* No `cc1plus`/`cmake` from another session was running when the worktree was
  created.
* Long-running work is always launched with `resource_guard`; never
  `-j$(nproc)`, never a bare `-j`.
