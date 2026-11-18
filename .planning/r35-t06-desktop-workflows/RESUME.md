# R35-T06 — RESUME

Everything a new shell must restore explicitly. The worktree keeps its `r33`
directory name on purpose (DECISIONS D-01) — that is not a mistake.

```bash
export EXPRS_TRACK=t06-desktop-workflows
export EXPRS_WT=/home/kevin/projects/rs-studio/exp-rs-r33-t06-desktop-workflows-20261117T141119Z
export EXPRS_BRANCH=zcode/r35-t06-desktop-workflows-20261118T150535Z
export EXPRS_BASE_BRANCH=master
export EXPRS_BASE_SHA=120677e6e165d3e88bb9d2ec2a3fd98042ec719d
export EXPRS_BUILD="$EXPRS_WT/build-dev"
export EXPRS_CONFIG=Debug
export EXPRS_PLAN="$EXPRS_WT/.planning/r35-t06-desktop-workflows"
export EXPRS_HOST_LOCK="$HOME/.cache/exp-rs-agent/host-build"
export CMAKE_BUILD_PARALLEL_LEVEL=4
export CTEST_PARALLEL_LEVEL=1
export OMP_NUM_THREADS=1 OPENBLAS_NUM_THREADS=1 MKL_NUM_THREADS=1
# MANDATORY: /tmp is a 10 MB tmpfs and g++ writes its assembler temp there.
export TMPDIR="$HOME/.cache/exp-rs-agent/tmp"
export TMP="$TMPDIR" TEMP="$TMPDIR"
mkdir -p "$TMPDIR"
```

## Configure (repeat verbatim; ~20–30 min generate)

```bash
cd "$EXPRS_WT"
python3 scripts/dev/resource_guard.py --stale-after 1 --lock-dir "$EXPRS_HOST_LOCK" --parallel 4 --max-parallel 4 --wait 600 -- \
  python3 scripts/dev/resource_guard.py --stale-after 1 --lock-dir "$EXPRS_BUILD" --parallel 4 --max-parallel 4 --wait 600 -- \
  cmake --preset dev-default \
    -DOpenCV_DIR=/home/kevin/projects/rs-studio/main/.scratch/ui-build-deps/opencv/usr/lib/cmake/opencv5
```

Without `-DOpenCV_DIR` the configure dies at `src/processing/CMakeLists.txt:12`.

## Build / test this track's targets

```bash
python3 scripts/dev/resource_guard.py --stale-after 1 --lock-dir "$EXPRS_HOST_LOCK" --parallel 4 --max-parallel 4 --wait 600 -- \
  python3 scripts/dev/resource_guard.py --stale-after 1 --lock-dir "$EXPRS_BUILD" --parallel 4 --max-parallel 4 --wait 600 -- \
  cmake --build "$EXPRS_BUILD" --config Debug --parallel 4 --target \
    test_r35_t06_preview_lifecycle test_r33_t06_preview_identity test_asset_preview_service
```

```bash
QT_QPA_PLATFORM=offscreen ctest --test-dir "$EXPRS_BUILD" -j1 -R 'R35-T06|R33-T06|preview' --output-on-failure
```

CTest names are the Catch2 **case titles** (`R35-T06 …`), case-sensitive; `-R`
on a target name matches nothing.

## Gates (no build needed)

```bash
python3 scripts/audit_i18n_strings.py --files tests/fixtures/i18n_audit_files.txt \
  --gate tests/fixtures/i18n_string_baseline.json --selftest     # canonical: must stay PASS
python3 scripts/check_translation_freshness.py                    # ~7 s here; must stay PASS
lrelease6 resources/translations/sicnu_zh_CN.ts -qm /tmp/x.qm      # must report 0 unfinished
```

## Gotchas that cost time already

* A fresh `git worktree add` on this host can take 40+ min and be killed by the
  tool timeout; prefer continuing in an existing clean worktree of this track.
* `resource_guard.py --stale-after 1` reclaims a lock whose holder pid is dead
  (a killed build leaves one). Never hand-delete a live lock.
* `tests/CMakeLists.txt` is append-only for this track; the delivery diff must
  show additions only in that file.
* `fish` is the default shell: no heredocs, no `VAR=...` assignment — put
  multi-step commands in a `#!/usr/bin/env bash` script file.
