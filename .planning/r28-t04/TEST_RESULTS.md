# TEST_RESULTS — R28-T04 final matrix (all local, QT_QPA_PLATFORM=offscreen, SICNU_CP11_RENDER=1)

> Post-merge review (2026-11-06): the 15/15 local matrix below is accurate,
> but CI Tier 1 was red with clang-tidy `-warnings-as-errors` (exit 123), and
> `main_window_vector.cpp` had a real null-vector-layer path. These are fixed
> in the post-merge review follow-up; "no unresolved blockers" in the original
> PR body referred only to the local matrix, not to CI.

Build: `cmake --build build-r28-t04 --parallel 2` under `scripts/dev/resource_guard.py` dual locks
(host `~/.cache/exp-rs-agent/host-build` → worktree build lock). Binaries from THIS worktree at HEAD
66158b4e5b (base 50cc3255c5). Logs: `.planning/r28-t04/green-logs/*.final{1,2}.log`,
baseline in `.planning/r28-t04/baseline-logs/`.

## Baseline (pristine tree, BEFORE any edit)
`test_cartography_production_11`: **3/32 RED** (atlas produce, byte-identical, corpus atlas templates) — the
#1812-admitted atlas delivery failures, reproduced. All other 14 family suites GREEN (incl.
test_spectral_profile_widget 16/16 — the #1561 FIXED_IN_MASTER spot-verify).

## Final (post all fixes + R1 fixes) — every suite run TWICE consecutively
| Suite | Result (identical both passes) |
|---|---|
| test_cartography_production_11 | 33/33 cases, **408 assertions** all passed (was 3 red) |
| test_orthorectification_dialog (+3 F7 cases) | 4/4 cases, 40 assertions |
| test_r28_t04_cartography_watchdog (F14, NEW) | 3/3 cases, 11 assertions |
| test_r28_t04_experiment_teardown (F18, NEW) | 1/1 case, 38 assertions (real 14000² study mid-flight; teardown <1.5 s; worker drained before exit) |
| test_r28_t04_layout_template_safety (F12, NEW) | 1 case, 27 assertions |
| test_r28_t04_import_batch (WP03, NEW) | 4/4 cases, 23 assertions |
| test_r28_t04_workflow_panel_consistency (WP04/05, NEW) | 6/6 cases, 294 assertions |
| test_workflow_session_controller | 5/5, 35 assertions |
| test_r25_d_cartography_dock / _ux / _e2e_display_cartography | green / green / green |
| test_layout_designer / _lifecycle | green / green |
| test_experiment_studio_dock | 8/8, 106 assertions |
| test_workbench_full_shell_lifecycle | 7/7, 126 assertions |

**New failures vs baseline: zero.** Exit codes all 0 (the earlier teardown/argv crashes are fixed and
documented in the commits).

## RED evidence per fix
- WP10: baseline logs (atlas_baseline.log, cp11_full.log) — 3 red against pristine tree; existing
  tests were the failing oracles; production fixed, assertions unchanged (corpus fixture gained its
  declared precondition — see commit messages; the byte-identical restructure is D10/H1).
- F18: new case asserts <1.5 s against the unfixed destructor (blocks for the op duration) —
  behavior-RED by construction; baseline `test_experiment_studio_dock` green pre-change proves no
  pre-existing teardown coverage conflict.
- F12/F14/WP03/WP04-05: compile-RED (new public APIs absent on base) + behavior verification above;
  defects' existence on base documented with file:line in ISSUE_TRIAGE.md / DECISIONS.md.
