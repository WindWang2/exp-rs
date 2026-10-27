# R24-C: Pi/MCP Harness × Qt/QGIS Workbench × Cartography — Experience, Contract & Stability Closure

Baseline `origin/master`: `5fe642ec6339807823aa56612ba94668592a738d` (2026-10-27T02:00Z snapshot)
Head: `<FILL>`

## What this PR does
Closes verified-open gaps that break the closed loop between the shipped Pi/MCP harness, the Qt/QGIS workbench and the cartography path — no new frameworks, no parallel surfaces, existing contracts only.

## Changes (atomic commits)
1. **workspace-state charts contract (WP1, src/agent)** — the tool surface has always promised `charts` in the structured WorkspaceState (tests/surface_diff_snapshot.json workspace_state description), but `buildWorkspaceState` hardcoded the array empty: the live `ChartRegistry` (the registry `cartography:chart_*` tools mutate) was never consulted. Now projected with stable entity ids (`chart-N` via WorkspaceEntityRegistry, persisted with the project like other kinds), `chart_id`/`kind`/`title`, bounded at 25 (chart_registry.h's declared summary cap). RED → GREEN ×2: tests/test_r24_c_workspace_charts.cpp (projection, id stability, empty-registry negative control, 30→25 bound, invalid-spec rejection).
2. **#1699 wall-clock gate conversion (WP2, 8 test files, 9 sites)** — r23-E's C-track assignment: raw `elapsed < N` gates converted to `sicnu::testsupport::wallClockBudgetMs(N)` (sanitizer ×4, SICNU_TEST_TIME_SCALE ad hoc; Release enforces base). Regime-separation windows (test_adversarial_m5 #798, test_agent_ops_ui_smoke #1493) use `min(wallClockBudgetMs(200), 400)` — scaled but never allowed to cross the 500ms regime boundary they classify.
3. **Layout designer menu honesty (WP3, src/app/layout)** — the port shipped action-less "Atlas"/"Report" menus in the designer's menu bar (advertising unshipped features). Menus no longer created; `atlasMenu()/reportMenu()` report nullptr per the port's existing atlasToolbar() convention; headless atlas export remains via cartography:export (exportMapAtlas). Regression: menu bar carries only menus with actions.
4. **teaching_admin GUI-thread + i18n (WP4, src/app/teaching_admin)** — #1695 items 2/3 (handed to C by #1713): submission discovery moved off the GUI thread into the batch worker (console shows honest "discovering…" then the preview), and the `"entr%2"+"y"/"ies"` concat replaced by a real `%n` plural form (+ zh_CN translation, numerus).
5. **Pi suite cross-platform honesty (WP5, pi/test)** — on a Windows checkout 23/41 pi tests false-failed from two non-product defect classes: CRLF-fragile assertions (bridge_parity F-PI-1 regex; knowledge-page budget counting checkout line-ending pads as content) and `spawn EFTYPE` shebang limitation without canaries in 17 tests. Now: LF-normalized content assertions + memoized host canary gating with explicit skip reasons. POSIX behavior unchanged (canary passes → full coverage).

## Fixed / guarded
- Verified already-fixed on master (NOT redone, evidence in ISSUE_TRIAGE.md): #1558, #1628-1/2/4/5, #1693, #1557, #1632, plus the #1692/#1705/#1690/#1691/#1704/#1694/#1689/#1678/#1688/#1697/#1698/#1681/#1706/#1707 stale set.
- Out of scope with reasons (CROSS_TRACK_FINDINGS.md): CT-D1 run_lab.cmd (scripts owner), #1695-5 publishBatchOutputsAtomic (teaching backend, read-only for C), #1628-3 temporal tool wiring (data-track decision), repair-engine DataType rule (needs an A-track dtype operator; registry verified to have none).

## Evidence (all commands run on this Windows host, MSVC/Ninja RelWithDebInfo build-r24_c)
- <FILL: RED/GREEN commands + exit codes>
- pi: `node --test pi/test` → 41 tests / 20 pass / 0 fail / 21 skipped (host spawn limitation, honestly skipped) / exit 0. POSIX CI runs the full set.
- Merge integrity: `python scripts/check_merge_registration_integrity.py --require-refs` exit 0.

## Reviews
- R1 (independent, fresh context): REVIEW_R1.md <FILL verdict>
- R2 (independent, clean-diff second context): REVIEW_R2.md <FILL verdict>

## Not run here
- Linux/macOS lanes (this is a Windows host; the pi CRLF/canary changes are POSIX-inert by construction — canary passes there).
- QGIS offscreen-render-limited visual assertions (structural menu/state checks used instead).
