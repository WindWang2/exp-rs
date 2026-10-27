# TASKS — R24-C

Execution order = RED→implement→GREEN×2 per work package; single focused change per loop round.

- [ ] T0 Recon & planning docs (BASELINE/TRIAGE/OWNERSHIP/DECISIONS/ORACLES/RISK/LOOP_LEDGER/PR_COLLISION_MATRIX/CROSS_TRACK_FINDINGS skeletons) — done at baseline
- [ ] T1 Build baseline: configure build-r24_c; build `sicnu_agent` + test target used by T2; record times
- [ ] T2 **WP1 charts wiring (L1)**: RED `tests/test_r24_c_workspace_charts.cpp` (create charts → buildWorkspaceState shows them w/ stable ids, bounded 25, cleared registry → empty); implement workspace_state.cpp wiring; GREEN×2
- [ ] T3 **WP2 test honesty (#1558/L2)**: re-verify tautologies at test_e2e_open_issues.cpp:817/871; replace with meaningful assertions (fail-capable); run file's tests ×2
- [ ] T4 **WP3 clock gates (#1699/L3)**: convert 8 files per r23-E three-way pattern (time_budget / hang-guard / structural) using tests/sicnu_test_time_budget.h; run each converted test ×2
- [ ] T5 **WP4 layout designer honesty (L4)**: hide empty Atlas/Report menus; test via existing test_layout_designer* harness (add case if applicable)
- [ ] T6 **WP5 i18n plural + dup using (L5+L8)**: `tr(...,nullptr,n)` plural for entry count; collapse data_platform_tools.cpp triple using stack; zh_CN .ts regen not required for code-level fix (verify lupdate markers)
- [ ] T7 **WP6 workspace_snapshot dead range (L7)**: remove unreachable Real Data Range branch + fix phantom comment OR wire real producer if raster_display_service already computes stats (inspect first)
- [ ] T8 **WP7 teaching_admin discoverSubmissions off-thread (L6/D-10)**: background scan + cooperative cancel; Qt event-loop test for non-blocking + completion
- [ ] T9 **WP8 repair rules (L9/D-4)**: inspect operator registry for type-cast/resample ops; add closed-form rules + RED tests only if deterministic op exists
- [ ] T10 O-C0 evidence: UI_FLOW_MATRIX.md ≥6 real paths w/ ctest/binary smoke log references
- [ ] T11 O-C1 evidence: E2E chain pi (fake MCP) + real `--mcp` binary smoke incl. cancel path (reuse tests/surface_mcp_host_main + test_r14_C_mcp_workflow_cancel runs; add missing coverage if gaps)
- [ ] T12 PERF_BASELINE.md: 5-run samples for changed paths
- [ ] T13 R1 independent review (fresh reviewer context) → fix P0-P2 → GREEN×2 re-run
- [ ] T14 R2 independent review (clean diff, second fresh context) → fix → re-run
- [ ] T15 Merge-integrity gate + `git diff --check` + fetch/rebase check + push + non-draft PR + FINAL_REPORT.md
