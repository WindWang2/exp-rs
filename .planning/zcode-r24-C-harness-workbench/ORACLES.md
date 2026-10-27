# ORACLES — R24-C objective acceptance

Mapping to the goal's O-C0..O-C5, with concrete evidence artifacts:

- **O-C0 (UI flow matrix)**: `UI_FLOW_MATRIX.md` with ≥6 real user paths (import→inspect→algorithm→layer result; select→params→run→cancel; Pi ask→tool discovery→run_workflow→status→map; layer edit→undo/redo→exit; MapSpec→preview→export; task failure→diagnose→recover). Each path cites code entry points (file:line) and an executed automated test (ctest name or binary smoke log). Evidence = TEST_RESULTS.md runs.
- **O-C1 (Pi→--mcp→TaskCenter/GUI E2E incl. cancel)**: at least one executed end-to-end chain: node --test pi suite (fake MCP) for bridge semantics + real `--mcp` binary smoke (surface_mcp_host / test_r14_C_mcp_workflow_cancel) exercising run→cancel with typed states. No parallel fake status table (same WorkflowRunCoordinator authority both sides — cited in code).
- **O-C2 (QGIS layer save/reopen, CRS change, lease delete/undo, export ±controls)**: existing test families re-run GREEN ×2 on this track's build (test_layers, test_active_view_host_*, test_r14_D_vector_edit_honesty, test_cartography_production_11, test_layout_designer_lifecycle) + this track's new regression (charts/workspace). No leak: RAII patterns unchanged; no new bare pointers introduced.
- **O-C3 (honest offline UI, i18n/HiDPI/keyboard, perf 5×)**: L4 menu honesty fix + L5 plural fix; PERF_BASELINE.md 5-sample before/after for changed code paths; no new hard wall-clock gates introduced (all timing assertions added are time_budget/hang-guard style).
- **O-C4 (MCP typed fail-closed, no #1714 regression)**: test_r22_agent_{containment,env_keys,ipc,ghost_algo,job_logs} + test_mcp_server re-run GREEN ×2; workspace-state change does not touch containment; charts projection reads registry under its own mutex only.
- **O-C5 (dual independent review, 2×GREEN, merge integrity 0-loss, non-draft PR)**: REVIEW_R1.md, REVIEW_R2.md from fresh reviewer contexts; all new/converted tests GREEN twice; scripts/check_merge_registration_integrity.py --require-refs exit 0; PR URL recorded in FINAL_REPORT.md.

## Exit condition
All 6 oracles evidenced, or specific BLOCKED/NOT_EXECUTED entries with reproduction commands.
