# TEST_RESULTS — R24-C

Host: Windows 10.0.26200, MSVC 14.38 + Ninja, Qt 6.8.0, RelWithDebInfo build-r24_c, QT_QPA_PLATFORM=offscreen.
Driver: `.planning/zcode-r24-C-harness-workbench/run_tests.py` (canonical DLL env: build root + Qt bin + vcpkg bin + qca/kc bins + PROJ_DATA; real exit codes — the interim cmd for-loop EXIT lines were void due to %ERRORLEVEL% block expansion and are superseded by this driver's results).

## Final evidence table

| Check | Command | Result |
|---|---|---|
| pi suite (after WP5) | `node --test pi/test/*.test.mjs` | 41 tests / 20 pass / 0 fail / 21 skipped (host spawn canary) / exit 0 — after advisory-R1 hardening re-run: same, exit 0 |
| pi suite (baseline before) | same | 18 pass / 23 fail (documents the Windows-checkout false-red fixed by WP5) |
| WP1 RED | `ctest -R test_r24_c_workspace_charts` (pre-fix) | 0/2 — `0 == 25` charts; exit 8 |
| WP1 GREEN ×2 | ctest + python driver | 2/2 PASS exit 0 (×2: 0.93+0.82 s; 0.90+2.69 s) |
| Neighborhood (workspace_state) | ctest -R "workspace_state produces\|stable short ids\|workflow runs provider" | 3/3 PASS, exit 0 |
| #1699 clock gates (8 binaries) | run_tests.py ×2 rounds | all PASS both rounds: model_failure_matrix 2158a/19c, execution_governor_11 145a/10c, temporal_profile_widget 8a/3c, feature_scatter 9a/3c, progress_dialog 21a/8c, adversarial_m5 81a/4c, tool_call_dispatcher 226a/27c, agent_ops_ui_smoke 12a/5c |
| Layout designer (menu honesty) | run_tests.py test_layout_designer ×2 | 57 assertions / 10 cases PASS ×2 |
| Surface gate | run_tests.py test_surface_snapshot ×2 | **pre-fix on master: FAIL 2/16531 (stale pin — see below); post re-pin: 17868 assertions / 7 cases PASS ×2** |
| Surface drift of this track | regen diff vs checked-in pin | 370/371 tools byte-identical; the 1 diff was master's own b43247dd7 schema-text drift (rs:sar_interferogram flattenRamp) |
| pi surface pins | `node --test pi/test/surface_snapshot.test.mjs` (post re-pin) | 7 pass / 0 fail |
| MCP server E2E (real stdio wire) | run_tests.py test_mcp_server ×2 | 9684/9684 assertions PASS ×2 |
| Workflow cancel chain | run_tests.py test_r14_C_mcp_workflow_cancel | 21 assertions / 2 cases PASS |
| **O-C1 real-binary smoke** | `EXP_RS_MCP_BIN=build-r24_c/surface_mcp_host.exe node .planning/.../r24c_real_binary_smoke.mjs` | PASS exit 0: initialize (server exp-rs-mcp) → tools/list 373 tools (incl. cartography:chart_*) → tools/call list_algorithms (real algorithm catalog) → unknown tool = typed JSON-RPC error frame via bridge → bridge alive → clean teardown |
| Merge integrity | `python scripts/check_merge_registration_integrity.py --require-refs` | exit 0 (re-run pre-push) |
| Whitespace | `git diff --check` | clean |
| Perf 5× | see PERF_BASELINE.md | 1136.0/867.9/844.1/859.5/849.3 ms (charts test whole-process; steady ≈ 850 ms, first-run cold +270 ms) |

## Master defect found & fixed en route
`test_surface_snapshot` fails on PRISTINE master: b43247dd7 (SAR flattenRamp wrapped-domain diagnostic) extended the operator schema description without re-pinning the three-way snapshot (last regen 3ff4e0ba4, 2026-10-21); CI has had no completed runs since 2026-10-11 (#1491). Fixed by regenerating the pin (single-line diff; commit 53dc38dd8).

## NOT_EXECUTED / not run here
- Linux/macOS lanes (Windows host): pi changes are POSIX-inert by construction (canary passes there, LF normalization is byte-neutral on LF checkouts); C++ changes are platform-independent test/source edits.
- sicnu_geo_rs full app binary + GUI smoke: not required by the oracles (surface_mcp_host exercises the production McpServer); the app target was not built (build-time budget).
- test_task_center "#1454 cancel-window" case: known flaky per #1714/#1677 evidence, out of this track's changes.
