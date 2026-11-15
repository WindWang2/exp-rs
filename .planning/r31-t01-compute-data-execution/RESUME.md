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

- Base: `85ce917bea0ece4df6548a677b97a72ecde8659b` (#2067). Test/ledger checkpoint: `313530ef9f`.
- Current implementation HEAD: `477c9dbdb2` (WP11). Other commits: `28f3be3be8` (WP02), `ec78ccfaa9` (WP03), `5314c8607c` (WP05), `8e53a04b73` (WP08), `8b6f98b2ab` (WP07), `bf11dab3b6` (WP15).
- #2061 full patch, 34-file set and review document read; relevant issue bodies/comments and merged R30 implementation evidence read. Its business paths remain active-exclude.
- All production changes remain **unverified**: no test has compiled or run. Do not expand scope before the RED/GREEN pass unless verification requires a fix.
- Configure `bash-57n2o6fa` succeeded after the bounded wait-3600: real Ninja files were generated. Background task `bash-i7t6laa3` is building the WP02 test-only baseline for RED.
- Upstream raw snapshots and the full #2061 patch are in `.scratch/r31-t01-upstream/` (local-only); configure logs are in `.scratch/logs/`.
- Original worktree has pre-existing untracked `.workbuddy/` and `PR_REVIEW_MERGE_2026-11-06.md`; do not touch them. The #2061 worktree is also preserved.

## Next actions

1. When configure completes, `git checkout 313530ef9f -- <WP02 production paths>` before the first test build to execute the mixed-report/multi-output RED; restore `28f3be3be8` for GREEN.
2. Build and run the result-semantics, workflow-resume, model-identity, VRT-reuse, VRT-NoData and sampling-admission targets, recording actual case counts and failures.
3. Run affected legacy families and finish WP04/WP16 controlled residuals; keep every WP state in `RESIDUAL_MATRIX.md` evidence-backed.
4. Keep `TEST_LOG_INDEX.md` and `ACCEPTANCE.md` synchronized with actual commands and exit codes.

## Processes and locks

- Background task `bash-57n2o6fa`: bounded wait-3600 double-guard configure; do not start another heavy command until its completion is known.
- Previous wait-1800 task `bash-x1136wwf` ended exit 75 without starting CMake. T02's live build held the lock; no process was killed and no lock deleted.
