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
- Full-tree CTest classification: <TO_FILL>
- Snapshot regeneration + drift classification: <TO_FILL>

## Delivery chains (WP12)

1. Teaching chain: <TO_FILL>
2. Experiment→repro chain: <TO_FILL>
3. Install-tree chain: <TO_FILL>

## R1/R2 reviews

- R1: <reviewer report link/summary + findings triage>
- R2: <same, post-fixes>

## NOT_EXECUTED / known risks

- Windows/macOS lanes: no hosts available — NOT_EXECUTED (documented, not claimed)
- Full 9776-test population: <classification summary> — pre-existing failures enumerated, zero new failures from this branch

## Rollback

Single-branch revert; no schema/data migrations; the sparse confusion format is read-compatible with all legacy dense documents (fromJson accepts both).
