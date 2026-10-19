# R18-C Final test results

Worktree `/home/kevin/project/exp-rs-r18-c-agent`, branch `agent/r18-c-pi-mcp-workflow`, rebased on `a60c8831c` (origin/master at delivery; started from `2d2881631`, no conflicts on rebase).
Build: `build-r18-c`, Debug, `-j2` under `/tmp/exp-rs-five-tracks-build.lock` + resource_guard; `QT_QPA_PLATFORM=offscreen`; runtime `LD_LIBRARY_PATH` per DECISIONS D12.

## Tooling
node v26.11.1; cmake 3.30 (Unix Makefiles); Catch2 v3.7.1 (offline source).

## FINAL VERIFICATION (post-rebase, post-R2-fixes)

### C++ (exit codes; each suite run TWICE, identical results)
| Suite | Round 1 | Round 2 |
|---|---|---|
| test_mcp_server | 0 — 9686 assertions / 31 cases | 0 — identical |
| test_agent_loop_seams | 0 — 101 / 10 | 0 |
| test_plugin_capabilities (build root) | 0 — 129 / 14 | 0 |
| test_workflow_execution_plane | 0 — 96 / 8 | 0 |
| test_workflow_recovery | 0 — 163 / 15 | 0 |
| test_agent_ops_task_center_executor | 0 — 101 / 28 | 0 |
| test_r18_c_progress_units | 0 — 13 / 1 | 0 |
| test_r18_c_rpc_cancel_id_namespace | 0 — 6 / 1 | 0 |
| test_r18_c_surface_pagination | 0 — 1125 / 3 | 0 |
| test_surface_protocol | 0 — 74 / 10 | 0 |
| test_r14_C_mcp_workflow_cancel | 0 — 21 / 2 | 0 |
| test_surface_parity | 0 — 5484 / 12 | 0 |

Post-R2-fix re-verification subset: surface_pagination 1125/3, cancel_id 6/1, mcp_server 9686/31, surface_parity 5484/12 — all exit 0.

### Pi (TypeScript)
`node --test pi/test/*.test.mjs` — **39/39 pass, exit 0** (×3 runs: after C05-a, after C09, final).

## RED evidence chain (Oracle-A)
1. **C00 pi**: restored r15-D tests vs reverted bridge — 4 fail (spawn-failure zombie, initialize-timeout zombie, desync teardown, runaway-line anchor); restored → 37/37 ×2.
2. **C00 C++ behavior**: `test_mcp_server "[stdio]"` 1 case/5 assertions failed (clean-EOF −32700, 4MiB boundary); `test_agent_loop_seams` 1 case failed (wrong-typed leaves); restored → all green ×2 (9686/31, 101/10).
3. **C00 compile-level**: test_plugin_capabilities failed to compile (`writeRootGateActive` undeclared) → green 129/14 after restore.
4. **C03-a**: fixes-reverted binary — progress_units 2 failed (wire 0.005; 0.60 never emitted) → 13/13.
5. **C05-b**: fixes-reverted binary — cancel_id 2 failed (numeric cancel stopped BOTH tasks; string entry consumed) → 6/6.
6. **C01**: initial parity sweep 11 schema mismatches (raw-vs-normalized + dual-source aliases) + cursor sections → 1125/3.
7. **C05-a**: timeout test 0 cancels observed → 1/1 ×2 (pi 39/39 overall).

## NOT_EXECUTED
- `test_capability_surface_parity` — 2 assertions fail on `help composition: cannot open embedded glossary resource :/terms/rs_glossary.json`: that test target does not compile `data/help/help_content.qrc` into its sources (by-design note in src/help/CMakeLists.txt:43-46), and this offline build tree has no embedded glossary. Independent of this branch's diff (help/glossary domain). Compensating verification: the four surface suites above (test_surface_parity, test_surface_protocol, test_r18_c_surface_pagination, test_mcp_server) all green.
- Sanitizer/race/leak runs — not executed (sanitizer preset needs its own full toolchain pass on a loaded five-track host; r15-D's own sanitized validation covered the restored code at its original merge). Flagged as follow-up.
- Windows junction test — SKIP by design on Linux (`mklink /J` unavailable); pinned on Windows runners per #1516.

## Sanitizer / race / leak
NOT_EXECUTED (see above). No new threading introduced by this branch: all changed C++ paths run on the Qt event loop (handleRequest / onTaskUpdated) or in existing operator threads.
