# REVIEW_R1 — R24-C (official independent review, fresh context)

- Reviewer: background general-purpose agent, fresh session (no authorship of any change under review).
- Scope: complete diff `origin/master(5fe642ec6)..d899169bf` (7 commits; product code in full, planning skimmed).
- Reviewer's own verification runs (this host): run_tests.py 3 binaries PASS (47a/2c, 57a/10c, 17868a/7c); pi suite 41t/20p/0f/21s exit 0; git-verified the stale-pin claim (master snapshot grep 0 vs source 2; branch snapshot diff = 1 line numstat 1/1).

## Verdict: NEEDS_FIXES (2×P2, 6×P3; no P0/P1)

| # | Sev | Finding | Disposition |
|---|-----|---------|-------------|
| 1 | P2 | workspace_state title `asString()` could throw on tool-accepted non-string title → whole contract degraded until chart deleted | FIXED: validateChartSpec rejects non-string titles (create+update); listChartSummaries type-guards every field; new negative-control test case |
| 2 | P2 | pi canary regex (EFTYPE|ENOENT|EACCES|EPIPE|spawn) could mask real regressions as green skips on POSIX; missing fixture (ENOENT) should fail loudly | FIXED: launch-class only (`^Failed to launch`|EFTYPE); everything else rethrows |
| 3 | P3 | listCharts() deep-copies all specs per capture; only ≤25 summaries used | FIXED: listChartSummaries(max, &total) projects under the mutex |
| 4 | P3 | chart id reuse after tail delete undermines entity referents; "persisted" overstated | FIXED: monotonic mNextOrdinal (clear() resets — full-reset contract); commit wording already scoped to entity-id persistence (specs remain session-scoped by design) |
| 5 | P3 | no charts_truncated flag | FIXED: reported like assets_truncated + tested (30→true, 1→false) |
| 6 | P3 | QMap lexicographic cap order un(der)documented | FIXED: documented on listChartSummaries API |
| 7 | P3 | teaching_admin discovery walked twice per batch; no cancel hook on the scan | DOCUMENTED in-code + CT-C6 cross-track (backend API is read-only for C) |
| 8 | P3 | committed __pycache__/*.pyc | FIXED: removed + already-covered .gitignore |

Cleared by reviewer (no action): charts-projection threading (mutex-guarded copies, main-thread idFor persistence = pre-existing pattern); layout menu interface contract (interface docs say menus may not exist; no dereferencing callers); #1699 type/scale math (Release parity, clamp below 500 ms boundary verified); LF normalization POSIX byte-neutrality; new-test discriminative power (one gap → finding 1, now closed).

## Post-fix re-verification
- test_r24_c_workspace_charts 59 assertions / 3 cases PASS ×2 (incl. new: non-string title rejected; untitled → ""; tail-delete never reuses ids; charts_truncated honest — the cross-case counter reset caught by the ×2 discipline).
- test_workspace_state exit 0; test_surface_snapshot 17868a/7c PASS; pi 41t/20p/0f/21s ×2.
- Fixes committed: `1470e89d9` (code) + `fc7f0df63` (ledgers).
