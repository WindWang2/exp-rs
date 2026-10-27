# ISSUE_TRIAGE — R24-C

Categories: OPEN_LIVE (verified in source at `5fe642ec6`) / FIXED_IN_MASTER / COVERED_BY_PR / BLOCKED / OUT_OF_SCOPE.

## Stale-issue detachment (verified against merged PRs; do NOT redo)
- #1704 #1690 #1691 #1692 #1705 #1693 → FIXED (PR #1714; #1693 dead branch documented removed at mcp_server.cpp:432-441).
- #1694 #1689 #1678 #1688 → FIXED (PR #1711).
- #1697 #1698 #1681 (+#1682 plan) → FIXED (PR #1708).
- #1706 → FIXED (PR #1709). #1707 → FIXED (PR #1712).
- #1595 residue/#1597 → FIXED (PR #1675).
- #1535 #1536 #1537 #1559 #1560 #1561 #1562 #1563 #1564 → source-verified fixed in #1677 recon.
- #1557 #1632 (six items) #1655 item 1 → closed by #1713 per its PR body.
- #1628-1 (clean-EOF bogus −32700) → FIXED: mcp_server.cpp:425-431 distinguishes eofbit+gcount==0.
- #1628-2 (prose fields rejected as paths) → FIXED: `isFreeTextSlot` exact-key exemption, whole-subtree, unknown keys stay scanned (mcp_server.cpp:172-207).
- #1628-3 (temporal phenology tools unwired) → declared DECISIONS D-160-1 follow-up (production wiring is a data-track integration, not C).
- #1607 → OUT_OF_SCOPE (src/python raw returns; python/plugins track).
- #1695-4 (recordAnswer whitespace) → src/lab, D-track scope.
- #1695-5 (publishBatchOutputsAtomic JSON/CSV generational split) → **src/teaching_admin/batch_assessment.cpp** = teaching grading backend, read-only for C → CROSS_TRACK_FINDINGS.
- #1557 (macOS notification UAF) → src/native, not C-owned.
- #1619/#1620 (GUI weak stubs, qgssettingsregistrygui) → QGIS vendored stub surface; C must not rewrite vendor stubs.

## OPEN_LIVE candidates selected for R24-C work (verified by reading code)

| # | Gap | Evidence (file:line @5fe642ec6) | WP | Status |
|---|-----|-------------------------------|----|--------|
| L1 | Workspace-state `charts` array is hardcoded empty; ChartRegistry (live singleton, `cartography:chart_*` tools) never surfaces in the cross-conversation contract | src/agent/workspace_state.cpp:381-383 vs chart_registry.h:46-70 (`listCharts()` exists, comment "workspace summary caps at 25") | WP-C4 | OPEN_LIVE |
| L2 | Two issue-regression Tier1 cases assert tautologies (`QList::size() >= 0`, jsoncpp `size() >= 0`) — false-green regressions (#1558, P1) | tests/test_e2e_open_issues.cpp:817 (#799), :871 (#784) — re-verify before edit | Test honesty | OPEN_LIVE (verify) |
| L3 | #1699 residual wall-clock assertions in 8 C-track test files (r23-E table) | test_model_failure_matrix.cpp:1000; test_execution_governor_11.cpp:376; test_temporal_profile_widget.cpp:170; test_feature_scatter.cpp:111; test_progress_dialog.cpp:112; test_adversarial_m5.cpp:173/181; test_tool_call_dispatcher.cpp:990; test_agent_ops_ui_smoke.cpp:72 | WP-C9 | OPEN_LIVE (verify each) |
| L4 | Layout designer Atlas & Report menus created with zero actions; `atlasPreviewEnabled()` hard-false; `setAtlasPreviewEnabled/setAtlasFeature` no-ops — UI advertises nonexistent features | src/app/layout/qgslayoutdesignerdialog.cpp:732-733, .h:66, .cpp:252-253 | WP-C6 | OPEN_LIVE |
| L5 | #1695-2 i18n: English plural built by string concat `"entr%2"+"y"/"ies"` — untranslatable | src/app/teaching_admin/teaching_admin_dock.cpp:427-432 (re-verify) | WP-C2 | OPEN_LIVE (verify) |
| L6 | #1695-3: `discoverSubmissions()` runs synchronously on the GUI thread; destructor joins batch thread cooperatively → close can hang on network dirs | src/app/teaching_admin/teaching_admin_dock.cpp:414, :41-43 (re-verify) | WP-C2/C8 | OPEN_LIVE (verify) |
| L7 | #1628-4: workspace_snapshot "Real Data Range" branch unreachable — no off-thread producer exists; comment references nonexistent `resolvePromptHeader` | src/agent/workspace_snapshot.cpp:89-94, :320-323 | WP-C3 | OPEN_LIVE |
| L8 | #1628-5: data_platform_tools.cpp stacks 3 duplicate `using namespace sicnu::experiment` + 3 duplicate `using BenchmarkService` with contradictory "build-unblock" comments | src/agent/data_platform_tools.cpp:80-110 | WP-C3 | OPEN_LIVE |
| L9 | WorkflowRepairEngine has no closed-form rule for DataTypeMismatch / DimensionMismatch | src/workflow/workflow_repair_engine.h:16-19 | WP-C4 | OPEN_LIVE (only if a registered operator provides a deterministic repair — check registry first) |
| L10 | MCP `resources/read`+`prompts/get` unimplemented (−32601); `resources/list`/`prompts/list` empty | src/agent/mcp_server.cpp:758-769 | WP-C3 | OPEN_LIVE but NO consumer (Pi uses tools only) → recorded DECISION, not implemented |

## Re-verification results (read at `5fe642ec6`, 2026-10-27)
- **#1558 (L2) → FIXED_IN_MASTER**: no `size() >= 0` / `CHECK(true)` remains in tests/test_e2e_open_issues.cpp; #799 case now asserts `taskId > 0` + terminal Completed status; #784 case asserts per-step param gating values. (grep across tests/*.cpp: zero tautology hits)
- **L7 (#1628-4) → FIXED_IN_MASTER (comment honesty)**: workspace_snapshot.cpp:89-95 now accurately documents that no producer fills dataMin/dataMax and the range line stays unreachable pending #634 — no phantom `resolvePromptHeader` reference remains. Deliberate documented limitation, not drift.
- **L8 (#1628-5) → FIXED_IN_MASTER**: data_platform_tools.cpp now carries ONE `using namespace sicnu::experiment` with an explanatory comment about the collapsed triple stack.
- **L9 → OUT_OF_SCOPE for C** (verified): operator registry has NO dtype-conversion operator (full rs:/gdal: id enumeration); adding one is A-track algorithm work. DimensionMismatch is not even emitted by the checker. Recorded in CROSS_TRACK_FINDINGS (CT-C5).
- **L1, L3, L4, L5, L6 confirmed OPEN_LIVE** with current line refs (see table; L5 at teaching_admin_dock.cpp:439-444, L6 at :425-426 — line drift from issue baseline noted).

## Verification commands used

- `gh pr list --state all --limit 30`, `gh issue list --state open --limit 200`, `gh issue view <n>`.
- Direct reads: workspace_state.cpp:290-410, mcp_server.cpp:160-230/405-476, chart_registry.h/.cpp:1175-1182, test_workspace_state.cpp (whole), CROSS_TRACK_FINDINGS of r23-D/E.
