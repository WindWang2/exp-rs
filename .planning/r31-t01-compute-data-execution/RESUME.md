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

Host: `cachyos-3080`; canonical host lock directory: `/home/kevin/.cache/exp-rs-agent/host-build`. Whole-host compile ceiling is 4; tests are serial. Never delete a live lock; a stale lock (dead holder, age > 3600 s) is broken by the guard itself.

## Current checkpoint (2026-11-16, rebased)

- Branch HEAD: `c9e327d632` — rebased cleanly (no conflicts) onto `origin/master = fa5495fd70` (#2068 Pi P2, #2069 Pi P3). 19 commits; 57 files (+3159/−194).
- Track-start base was `85ce917bea` (#2067). Upstream overlap with this branch is only the additive `TaskCenter::isExecutionQuiescent`.
- #2061 still open (head `35f7a622`): its business paths remain active-exclude.
- Implemented and verified: WP02/WP03 (RED exit 42 → GREEN exit 0, 4 cases/99 assertions), WP04, WP05, WP07, WP08, WP11, WP15, WP16, WP17, WP18 (CPU/HTTP scope). 38 of 40 affected legacy suites green (329 cases / 117,720 assertions) on the pre-rebase HEAD; the 2 R30 VRT suites had a test-only glob fix pending re-verification.
- Independent R1/R2 review is **BLOCKED**: every `Agent` launch fails with a provider monthly-quota error. A self-review is recorded in `REVIEW_R1.md` (S1 fixed; S2–S4 disclosed).

## In flight

- Background task `bash-v0zu2o3g`: final double-guard build of all 40 affected targets on the rebased HEAD followed by the serial suite run; log `.scratch/logs/final_verify.log`. It waits (up to 2 h) for the host lock held by the T03 track.

## Next actions

1. When `bash-v0zu2o3g` finishes: read `FINAL_EXIT`, the per-target lines and `.scratch/logs/testrun/*.log`; record the rebased-HEAD numbers in `TEST_LOG_INDEX.md`/`ACCEPTANCE.md`. Any new failure must be A/B'd against the pre-rebase results before being called a regression.
2. Retry the independent review (`Agent`, plan type) once more; if the quota still blocks, push with the blocker stated in the PR body.
3. `git diff --check`, confirm staging is clean, then push the branch and create the PR from `PR_BODY.md` (`--base master --head zcode/r31-t01-compute-data-execution-20261115T135418Z`). Do not merge, do not wait for CI, do not close others' issues/PRs.
4. WP09/WP10/WP12/WP13 remain NOT_EXECUTED with evidence in `RESIDUAL_MATRIX.md`; WP06/WP14/WP19 are closed with suite evidence.

## Processes and locks

- `bash-v0zu2o3g` (final verify, waits for host lock). T03 (`exp-rs-r31-t03-desktop-cartography-20261115T135758Z`) holds the host lock; do not kill their processes or delete their lock.
