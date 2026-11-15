# R31-T01 RESUME

## Restore in every new shell

```bash
export EXPRS_TRACK="t01-compute-data-execution"
export EXPRS_REPO="/home/kevin/projects/rs-studio/main"
export EXPRS_BASE_BRANCH="master"
export EXPRS_BASE_SHA="85ce917bea0ece4df6548a677b97a72ecde8659b"
export EXPRS_STAMP="20261115T135418Z"
export EXPRS_BRANCH="zcode/r31-t01-compute-data-execution-20261115T135418Z"
export EXPRS_WT="/home/kevin/projects/rs-studio/exp-rs-r31-t01-compute-data-execution-20261115T135418Z"
export EXPRS_BUILD="$EXPRS_WT/build-dev"
export EXPRS_PLAN="$EXPRS_WT/.planning/r31-t01-compute-data-execution"
export EXPRS_HOST_LOCK="$HOME/.cache/exp-rs-agent/host-build"
export CMAKE_BUILD_PARALLEL_LEVEL=4
export CTEST_PARALLEL_LEVEL=1
export OMP_NUM_THREADS=1
export OPENBLAS_NUM_THREADS=1
export MKL_NUM_THREADS=1
cd "$EXPRS_WT"
```

Host: `cachyos-3080`; canonical host lock directory: `/home/kevin/.cache/exp-rs-agent/host-build` (guard lock file is its sibling `host-build.agent-build.lock`). Whole-host compile ceiling is 4; tests are serial. Never delete a live lock.

## Current checkpoint (after upstream/read-only triage)

- Base/initial HEAD: `85ce917bea0ece4df6548a677b97a72ecde8659b` (#2067).
- #2061 full patch, 34-file set and review document read; relevant issue bodies/comments and merged R30 implementation evidence read. Its business paths remain active-exclude.
- WP01/WP02 are in progress. `tests/test_r31_t01_result_semantics.cpp` contains the first mixed-report RED oracle, but it has not compiled or run yet; no production behavior has changed.
- `.gitignore`, a unique R31-T01 block in `tests/CMakeLists.txt`, the new test, and this track's Markdown ledgers are the only current edits.
- Configure attempt 1 failed on missing OpenCV discovery; attempt 2 exited 75 because T02 held the host lock. The wait-300 attempt then hit the host task timeout without starting CMake. A bounded wait-1800 retry is running with the confirmed local OpenCV prefix.
- Upstream raw snapshots and the full #2061 patch are in `.scratch/r31-t01-upstream/` (local-only); configure logs are in `.scratch/logs/`.
- Original worktree has pre-existing untracked `.workbuddy/` and `PR_REVIEW_MERGE_2026-11-06.md`; do not touch them. The #2061 worktree is also preserved.

## Next actions

1. Complete configure, build `test_r31_t01_result_semantics`, and run the mixed-report test against unmodified production code to record actual RED.
2. Implement descriptor-driven report/artifact separation only after that RED; then add real `io:*` chain and producer-control slices.
3. Continue WP04/WP05/WP08/WP15/WP16 controlled residuals; keep every WP state in `RESIDUAL_MATRIX.md` evidence-backed.
4. Keep `TEST_LOG_INDEX.md` and `ACCEPTANCE.md` synchronized with actual commands and exit codes.

## Processes and locks

- Background task `bash-57n2o6fa`: bounded wait-3600 double-guard configure; do not start another heavy command until its completion is known.
- Previous wait-1800 task `bash-x1136wwf` ended exit 75 without starting CMake. T02's live build held the lock; no process was killed and no lock deleted.
