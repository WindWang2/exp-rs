# R24-C: Pi/MCP Harness × Qt/QGIS Workbench × Cartography — Experience, Contract & Stability Closure

Baseline `origin/master`: `5fe642ec6339807823aa56612ba94668592a738d` (2026-10-27T02:00Z snapshot)
Head: see `git log origin/master..HEAD` (11 commits incl. the R1/R2 review-fix rounds), worktree branch `agent/r24_c-20261027T022556Z`

## What this PR does
Closes verified-open gaps that break the closed loop between the shipped Pi/MCP harness, the Qt/QGIS workbench and the cartography path — no new frameworks, no parallel surfaces, existing contracts only. Along the way it found and fixed a **surface-drift defect already failing on pristine master**.

## Changes (atomic commits, oldest→newest)
1. **`a266853a2` Pi suite cross-platform honesty (pi/test, 6 files)** — on a Windows checkout 23/41 pi tests false-failed from two non-product defect classes: (a) CRLF-fragile assertions (bridge_parity's F-PI-1 regex anchoring `\n`; the knowledge-page budget counting checkout line-ending pads as content: 32521 LF-bytes + 419 CRLF pads = 32940 > 32768); (b) `spawn EFTYPE` (fake server shebang) without canaries in 17 tests. Fixes: LF-normalized content assertions; memoized host canary with spawn-CLASS error classification (non-spawn failures rethrow so real regressions still fail loudly); temp-dir cleanup; POSIX behavior unchanged (canary passes → full coverage; normalization byte-neutral on LF checkouts).
2. **`0d29d2db2` workspace-state charts contract (src/agent)** — the workspace_state tool surface has always promised `charts` in the structured document (description pinned in tests/surface_diff_snapshot.json), but `buildWorkspaceState` hardcoded the array empty — the live ChartRegistry (which `cartography:chart_*` mutates) was never consulted, so agents could not see chart referents across conversations. Now projected with stable entity ids (WorkspaceEntityRegistry kind "chart", persisted with the project), chart_id/kind/title, bounded by a new single-source `kMaxWorkspaceSummaryCharts` (25). RED → GREEN×2 (new tests/test_r24_c_workspace_charts.cpp: projection, id stability, empty-registry negative control, 30→25 bound, invalid-spec rejection); test_workspace_state 3/3 still green.
3. **`53dc38dd8` surface snapshot re-pin (tests/surface_diff_snapshot.json, 1 line)** — **fixes a master defect**: b43247dd7 (SAR flattenRamp wrapped-domain diagnostic) changed the operator schema text without re-pinning the three-way snapshot → `test_surface_snapshot` failed on pristine master (2/16531 assertions). Regenerated on this build; regen diff proved 370/371 tools byte-identical and this track's own surface drift = zero. Post-pin: 17868 assertions / 7 cases PASS ×2; pi surface pins 7/7.
4. **`00f946694` #1699 wall-clock gates (8 test files, 9 sites)** — the r23-E C-track assignment: plain latency/hang guards → `wallClockBudgetMs(base)` (Release parity, sanitizer ×4, SICNU_TEST_TIME_SCALE); the two regime-separation windows (#798, #1493) → `min(wallClockBudgetMs(200), 400)` so no scale can classify a full-timeout/blocking wait as fast. All 8 binaries PASS ×2 with real exit codes (2158/145/8/9/21/81/226/12 assertions).
5. **`116a78aa4` layout designer menu honesty (src/app/layout)** — the port shipped action-less Atlas/Report menus advertising unshipped features; menus no longer created, capability accessors report nullptr per the existing atlasToolbar() convention, headless atlas export unaffected. New regression: menu bar carries only menus with actions (57 assertions/10 cases ×2 PASS).
6. **`d899169bf` teaching_admin GUI-thread + i18n (src/app/teaching_admin + zh_CN)** — #1695 items 2/3 (handed to C by #1713): submission discovery moved off the GUI thread into the batch worker (honest "discovering…" state, preview posted back queued; runBatchAssessment's own discovery unchanged), and the untranslatable `"entr%2"`+y/ies concat became a real `%n` plural with a numerus zh_CN entry.
7. **`af1969d17` planning/evidence ledgers** (force-added per r23-track precedent).
8. **R1 review round (`1470e89d9`)** — closed the official R1 findings: chart title type-safety on create+update (validateChartSpec rejects non-string titles; listChartSummaries type-guards every field), launch-class-only canary classification (`^Failed to launch`|EFTYPE — spawned-then-died/ENOENT/EPIPE rethrow), `listChartSummaries` (no full-spec deep copies per capture) + `charts_truncated`, monotonic chart ids (deleted ids never reused; clear() resets), QMap order documented, double-discovery documented (CT-C6), committed .pyc removed.
9. **R2 review round (final commit)** — closed the official R2 findings: updateChart title-rejection regression (negative patch refused, stored spec untouched; positive patch lands — 63a/3c PASS ×2), D-10 amended to the shipped shape + close-time join window documented in-code, teaching dock smoke evidence (4/4 non-cancel cases PASS ×2 incl. rapid-destruction over the new worker path; the 5th case fails IDENTICALLY on pristine master in this environment — slow python stub grades 0 rows here, verified by rebuilding the test against origin/master's dock — pre-existing, not a regression), .pyc re-add reverted, docs/PR-body staleness fixed.

## Evidence (Windows host, MSVC 14.38 + Ninja, RelWithDebInfo build-r24_c, offscreen)
- pi: `node --test pi/test` → 41 tests / 20 pass / **0 fail** / 21 skipped (host spawn canary, honest) / exit 0 (baseline before: 18 pass / 23 fail).
- O-C1 real-binary chain: `EXP_RS_MCP_BIN=build-r24_c/surface_mcp_host.exe node .planning/zcode-r24-C-harness-workbench/r24c_real_binary_smoke.mjs` → PASS: initialize (server exp-rs-mcp) → tools/list 373 tools → tools/call list_algorithms (real catalog) → unknown tool typed JSON-RPC refusal via bridge → alive → teardown. Plus test_mcp_server 9684/9684 ×2 and test_r14_C_mcp_workflow_cancel 21/2.
- Full table incl. per-binary assertion counts and 5-sample perf (≈850 ms steady whole-process, first-run +270 ms cold) in `.planning/zcode-r24-C-harness-workbench/TEST_RESULTS.md` / `PERF_BASELINE.md`.
- Merge integrity: `python scripts/check_merge_registration_integrity.py --require-refs` exit 0 (re-run at push time).

## Verified NOT-redone (evidence in ISSUE_TRIAGE.md)
#1558, #1628-1/2/4/5, #1693, plus the stale #1692/#1705/#1690/#1691/#1704/#1694/#1689/#1678/#1688/#1697/#1698/#1681/#1706/#1707 set — each re-checked against source before this track skipped it.

## Out of scope with reasons (CROSS_TRACK_FINDINGS.md)
CT-D1 run_lab.cmd `--lab ndvi_landsat` one-liner (scripts owner; not a C E2E blocker) · #1695-5 publishBatchOutputsAtomic split (src/teaching_admin backend, read-only for C) · #1628-3 temporal tool wiring (data-track decision) · repair-engine DataType rule (no dtype-conversion operator exists in the registry — A-track work).

## Reviews
- R1 (independent fresh context): NEEDS_FIXES 2×P2 + 6×P3 → ALL CLOSED in `1470e89d9`; re-verified GREEN ×2 (REVIEW_R1.md).
- R2 (independent clean-diff second context): NEEDS_FIXES 4×P3, product code verified correct/platform-inert → ALL CLOSED in the R2 round commit (REVIEW_R2.md).

## Not run here
Linux/macOS lanes (Windows host; pi changes POSIX-inert by construction, C++ edits platform-independent). Full app binary GUI smoke (not required by oracles; surface_mcp_host exercises the production McpServer). test_task_center "#1454 cancel-window" known-flaky per #1714/#1677 evidence.
