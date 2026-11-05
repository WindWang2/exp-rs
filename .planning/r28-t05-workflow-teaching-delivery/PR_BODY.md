# [R28-T05] workflow/experiment/teaching delivery: confirmed-open closures + honest gates + real delivery chains

<!-- Filled from real runs only; placeholders below are replaced before PR creation. -->

## Baseline & integration

- BASE_SHA: 50cc3255c5 (= origin/master at branch creation, re-verified)
- Integration SHA at PR: <TO_FILL after final merge>
- Open PRs at start: #1813 (T04 UI; touches tests/CMakeLists.txt — append-only coexistence)

## What this does (user-visible)

- Workflow placeholder integrity: malformed `${task.12abc}` refs can no longer silently bind task 12 (#1804/WE-18)
- Grader: last-ulp-equal metric recordings are no longer "conflicting truth"; the evidence index can no longer read freed memory when the caller's vector moves (#1804/WE-19)
- Metrics honesty: all-zero-truth MAPE reports undefined (JSON null + mape_defined=false), never a fake 0 (#1804/WE-24); confusion matrices accept legitimate large taxonomies (sparse storage, declared 4Mi-cell budget) instead of refusing at 8192 labels / allocating 512 MB (#1804/WE-25)
- Planner context refuses negative/non-finite resolutions (#1804/WE-26)
- Reproduction bundles: streamed, budgeted relocation; conflicts are reported and skipped instead of last-writer-wins overwrite; **dry-run mode** checks every gate and writes nothing (#1804/WE-29/WE-30 + WP07)
- Replay deviation: environment drift decides identity only under mutual Strict determinism — the remediation text and the verdict finally agree (#1772/WE-09)
- run_bridge never calls diagnostics().first() on an empty vector (#1804/WE-34)
- Configure honesty: a host without OpenCV now fails the configure with the remedy instead of dying ~1100 files into the build

## Issue status (per sub-item)

| Issue | Sub | Status |
|---|---|---|
| #1804 | WE-18/19/24/25/26/29/30/34 | Fixes #1804 (all eight confirmed-open sub-items closed; see per-sub evidence) |
| #1772 | WE-09 | Fixes #1772 |
| #1769/#1774/#1778/#1779 | WE-06/13/17/20 | Already fixed on master (verified in-tree + suites green) — Refs |
| #1736/#1748 | BCT-05/11 | Already fixed — Refs |
| #1697/#1698/#1623 | offline bundle/install/qm | Verified fixed by real install/bundle runs below — Refs |

## Shared interfaces / collision notes

- tests/CMakeLists.txt: append-only R28-T05 block (4 suites) at file tail — coexists with #1813's UI registrations
- src/planner/planning_context.cpp (T03 dir): 12-line fail-closed field guard, handoff note in .planning/.../HANDOFF.md
- CMakeLists.txt + src/processing/CMakeLists.txt: configure-time OpenCV honesty (global build rules = T05 primary)

## Test evidence

- Configure: exit 0 (Debug, ENABLE_TESTS=ON, ccache, OpenCV 5.0.0 local prefix)
- New suites: 14/14 ×2 green (offscreen, FAST lane)
- Red evidence: same tests on 50cc3255c5 → 5/10 fail at exactly the fixed defects (worktree exp-rs-r28-t05-redbase)
- merge-registration integrity: base→HEAD = +4 targets, 0 lost, 0 markers, 0 duplicates
- Full-tree CTest inventory: 9,848 registered tests (1,180 suites; the partial-build `_NOT_BUILT` placeholders disappear once the tree is built — they are honest "Not Run" failures, not green).
- New suites: 17/17 green twice (offscreen, FAST lane, ~45 s).
- RED evidence (baseline 50cc3255c5 worktree): 5/10 runnable new cases fail exactly at the fixed defects (WE-18/25/26/19/09).
- Fast-lane classification (`ctest -j1 -LE SLOW|LONG_RUN`, full tree): **9,793 tests, 71 failed (99% pass), ZERO new from this branch** — all 71 attributed to six pre-existing clusters (TaskCenter cancellation, GUI-teardown SEGFAULTs, cartography/atlas, contract/help dangling refs, network-policy R4-03 [confirmed failing on the untouched baseline], misc tail). `committed snapshots equal the fresh generation byte-for-byte` PASSES with the regenerated snapshot.
- Snapshot regeneration: `tests/surface_diff_snapshot.json` regenerated with the repo generator (`surface_snapshot_regen`, 371 tools) — the milestone #1812 marked blocked on a missing OpenCV toolchain. Drift classified as REAL product changes (#1739 demUnit alias table; #1733 approve-property removal); regeneration is idempotent (identical md5 across runs); drift suites 7/7 green; contract_graph/determinism census regenerate byte-identical.

## Delivery chains (WP12)

1. Teaching chain (offline sample → pipeline → grading): 7/8 PASS — sample generation, offline+recorded run, grading with explainable feedback (real verdict fail/score 45 + deductions), missing-artifact typed refusal (rc=2), garbage SICNU_PIPELINE_TIMEOUT_SEC bounded, rerun-after-error. The one FAIL records a NEW confirmed-open defect (below).
2. Experiment→repro chain: experiment inspect (run completed) + reproduction-bundle export (11 members, checksums) + validate (honest level=impossible with reason "run has no dataset pin").
3. Install-tree chain: **10/10 PASS** — real `cmake --install` into a staging prefix; CLI + sicnu_worker + zh_CN.qm installed; env-doctor and the operator registry load under a scrubbed environment; zero missing shared libs; no source-worktree linkage; a recorded experiment run from the INSTALLED tree.
   Library-level import safety (WE-29/30 + dry-run) is pinned by test_r28_t05_bundle_import_safety (the CLI surface intentionally exposes export|validate|inspect only).

## R1/R2 reviews

- R1 (independent, fresh context): 3 P1 + 4 P2 + 7 P3 — ALL P1/P2 + P3-12/13 fixed in 0b980cf3ed with new regressions; P3-8/9/10/11 deferred with impact+rationale in DECISIONS D8. Report: .planning/r28-t05-workflow-teaching-delivery/REVIEW_R1.md.
- R2 (independent, fresh context, post-fix): <filled when the report lands>

## NOT_EXECUTED / known risks

- Windows/macOS lanes: no hosts available — NOT_EXECUTED (documented, not claimed).
- NEW CONFIRMED-OPEN DEFECT FOUND BY THIS TRACK (not fixed here — T02 execution lane): **D-T05-CHAIN1** — `sicnu_geo_rs_cli --pipeline <doc>` reports "Pipeline succeeded" and "[Step 1/1 100%] Finished <op>" while the declared output file is never created (zero tiles sinked). Reproduces 10/10 in a fresh directory via the CLI tracked-pipeline path; the same operator writes correctly through the direct `runFromJson` seam used by tests. Same false-green family as the #1812-recorded TaskCenter cancellation cluster. Details + repro in ISSUE_TRIAGE.md / chain1_teaching.sh.
- Host reboot interrupted one mid-run verification; libraries verified intact and the run re-done.

## Rollback

Single-branch revert; no schema/data migrations; the sparse confusion format is read-compatible with all legacy dense documents (fromJson accepts both).
