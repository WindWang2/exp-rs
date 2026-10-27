# FINAL_REPORT — R24-C

- PR: **https://github.com/WindWang2/exp-rs/pull/1720** (OPEN, non-draft, base master)
- Head SHA: `2ecf381082a7187c7ff41c5d4cf589facd3113dd` (branch `agent/r24_c-20261027T022556Z`, 11 commits)
- Baseline: `origin/master` `5fe642ec6339807823aa56612ba94668592a738d` (unchanged at push; siblings #1718/#1719 open — no path overlap except the append-only tests/CMakeLists.txt union; neither touches surface_diff_snapshot.json)
- Merge integrity at push: `1042 targets / 49 add_test (base 1041/49; lost 0+0; added 1+0)`, exit 0. `git diff --check` clean.

## Delivered (goal WP mapping)
| Goal WP | Delivered |
|---|---|
| WP-C0 flows | UI_FLOW_MATRIX.md — 6 real paths with file:line code evidence + executed test references |
| WP-C1/C6 honesty | Atlas/Report action-less menus removed (+regression ×2); headless atlas export untouched |
| WP-C2 | #1695-2 real %n plural + numerus zh_CN; #1695-3 discovery off GUI thread (D-10 amended shape) |
| WP-C3/C4 contract | workspace-state `charts` projection wired to the live ChartRegistry (the surface already promised it) with stable entity ids, bound 25 + truncated flag, title type-safety (create+update), monotonic ids; master's STALE surface pin re-pinned (test_surface_snapshot failed on pristine master — merge-loss-class defect, CI dark since #1491) |
| WP-C3 reliability | pi suite Windows-checkout honesty: 23 false-fails → 0 fail/21 honest skips; canary classification launch-class-only after R1 |
| WP-C5 | single-fact-chain evidence: test_mcp_server 9684/9684 ×2 + r14_C cancel + real-binary Pi-bridge smoke (initialize/discovery/call/typed-refusal/liveness, exit 0) |
| WP-C9 | #1699: 8 files/9 sites converted (scaled budgets + clamped regime windows), real-exit-code ×2; CT-D1 recorded (not C's blocker); #1558 verified already fixed |
| WP-C8 | perf 5-sample baseline; zero new wall-clock gates |

## Oracles
- O-C0 ✔ (UI_FLOW_MATRIX + ctest/binary evidence) · O-C1 ✔ (real-binary chain incl. typed refusal; cancel covered by r14_C + mcp_server suites) · O-C2 ✔ (layer/lease/export families re-run green; no leak patterns introduced) · O-C3 ✔ (menu honesty, plural i18n, offline-honest skips, 5× perf) · O-C4 ✔ (mcp_server + r22_agent family green; no sandbox surface touched — surface regen diff proved 0 tool drift from this track) · O-C5 ✔ (R1 NEEDS_FIXES→closed, R2 NEEDS_FIXES→closed; GREEN×2 everywhere; integrity 0-loss; PR #1720 non-draft).

## Honest limits (NOT_EXECUTED / pre-existing)
- Linux/macOS lanes not run (Windows host); pi changes POSIX-inert by construction (reviewer-verified), C++ edits platform-neutral.
- test_teaching_admin_dock_smoke `[cancel]` case: fails IDENTICALLY against pristine master's dock in this environment (attribution experiment recorded in LOOP_LEDGER round 13; slow python stub grades 0 rows here). Not a regression of this PR.
- test_task_center "#1454 cancel-window" known-flaky (per #1714/#1677), untouched.
- Discovery scan has no cooperative cancel (backend API gap → CT-C6); close-time join bounded by one directory walk, documented.
- Repair-engine DataType rule needs an A-track dtype operator (CT-C5).

## Loop stats
15 loop rounds (LOOP_LEDGER); 2 independent reviews (fresh contexts) + 1 advisory pre-review, all findings closed; 11 commits.
