# R14-C Issue / PR Review Matrix

Verdicts verified against worktree @ f72f0c117 by five read-only audit agents (2026-10-15) with file:line evidence and fixing-commit ancestry (`git merge-base --is-ancestor`). Status vocab: OPEN (reproduced in code) / FIXED_ON_MASTER (commit cited) / FIXED_IN_OPEN_PR (#1505) / STALE / UNRELATED / NEEDS_REPRO.

## Track-relevant open issues

| Issue | Claim | Verdict | Evidence |
|---|---|---|---|
| #1499 | python_worker_process_pool.cpp compile break at master HEAD | **STALE** (fixed by #1501-era commits; file at HEAD has balanced namespaces; last commit 56418a418) — compile-verified in build-r14-C | src/python/isolated/python_worker_process_pool.cpp:765-779 |
| #1462 (python part) | startWorker failure strands zombie node | **FIXED_ON_MASTER** 1f4062bf2 + 56418a418 + ed3b1cb6c | pool:618-656, 731-763 |
| #1462 (IPC part) | unknown-method IPC gets no reply → wedged daemon | **FIXED_ON_MASTER** 7652b1190 + 83488c674 | app_interface_bridge.cpp:505; worker_daemon.py:25-26 |
| #1462 (plugin part) | duplicate operator registration hijack | **FIXED_ON_MASTER** e0664f65f (plugin-vs-plugin); **remaining half FIXED by this track (FIX-6)**: plugin-vs-BUILTIN shadowing unfixed at HEAD | plugin_runtime_host.cpp:193-204/412-423 |
| #1463 (×3) | journal goal-normalization eviction; bench prefix containment; recipe reload throws | **FIXED_ON_MASTER** f2e492087 | session_journal.cpp:108-130; trace.cpp:92-163; recipe_registry.cpp:72-86 |
| #1460 (×4) | advisory validateArgv hard gate; wrong-CWD argv[0]; PATHEXT; batch fail-as-ok | **FIXED_ON_MASTER** c8c463d22, dc8d43659, 27837c555 | external_process.cpp:600/1114, :291-318, :251-276; cli_batch_runner.cpp:476-487 |
| #1459 (×4) | optional deref; substring fallback; IO under mutex; whole-file digest | **FIXED_ON_MASTER** e7e4f77a0 | workflow_runtime.cpp:277-283/606-621; workflow_run_coordinator.cpp:1660; scratch_registry.cpp:44-89 |
| #1448 (1,2) | scan fail-open; checkpoint restore without validation | **FIXED_ON_MASTER** c43e8beae (+ others) | dataset_store_facets.cpp:195-258; workflow_run_coordinator.cpp:1358-1382; pipeline_run_coordinator.cpp:1290-1338 |
| #1448 (3, Engine-1) | terminal persist failure releases ownership | **FIXED_ON_MASTER** d5ab7bc0b | workflow_run_coordinator.cpp:549-563, 1047-1053; test at test_workflow_run_coordinator.cpp:1094 |
| #1448 (3, Engine-2/D17) | same for the D17 lane | **OPEN → FIXED by this track (FIX-2)** | pipeline_run_coordinator.cpp:1007-1013 (pre-fix) |
| #1447 | untyped jsoncpp escapes (9 sites) | **FIXED_ON_MASTER** c8c463d22; one NEW same-class site found → **FIXED by this track (FIX-3)** | operations_coordinator.cpp:340 (pre-fix) |
| #1486 | worker pool handleWorkerLoss data race | **FIXED_IN_OPEN_PR #1505** (master already has 56418a418 node-lifetime mutex; #1505 adds serverForWorker lookup) — dedupe, C does not touch | #1505 diff |
| #1454 (×3) | cancel-vs-submit race; committer temp retention; logBuffer copies | **FIXED_ON_MASTER** c8c463d22, d9e599d5f (+ 8833653f0) — all in src/processing (track B) | task_center.cpp:3039-3095; output_committer.cpp:285-372; task_center.cpp:94-109 |
| #1493 | agent ops startWorker GUI-thread join | **FIXED_ON_MASTER** c3a9aabf8 + 017594322 (src/app — track D) | agent_ops_control_center_dock.cpp:175-212 |
| #1450 | bounded reader not applied to checkpoint loads | **FIXED_ON_MASTER** | workflow_checkpoint.cpp:47-62, 141-164, 309-317 |

## New defects found by this track's audit (no issue existed)

| ID | Defect | Severity | Disposition |
|---|---|---|---|
| MCP-D2 | run_workflow/resume_workflow cannot be cancelled server-side (cancel map only fed by execution_id results) | P1 | **FIX-1** |
| Bridge-D3 | fastCrashCount never reset on healthy initialize → permanent respawn disable | P2 | **FIX-4** |
| Bridge-D4 | no outbound size guard; one >4 MiB call → id:null error rejects ALL pending | P2 | **FIX-4** |
| Plugins-C2 | data-provider id take-over across plugins | P2 | **FIX-5** |
| Plugins-C1 | manifest/binary operator can shadow BUILTIN rs: ids | P2 | **FIX-6** |
| SDK-C4 | explicit child-env entries have no loader-injection deny-list | P2 | **FIX-7** |
| SDK-C3 | BSD execvpeCompat resolves PATH from child envp (probe/exec divergence) | P2 | **FIX-7** |
| WF-C2 | E6003 classification keyed on error-message substring | P3 | **FIX-8b** |
| MCP-D1 | scientific:agent_session listed but permanently dead under --mcp (setAgentOpsDriver has no caller) | P2 | **BLOCKED_WITH_EVIDENCE** — honest fix requires wiring the driver in src/app/main.cpp (track D) or a surface-snapshot-wide relisting churning 4 pinned parity suites; deferred with evidence in PR |
| Catalog-D5/D6 | stale listing after late registry registrations; provider-name collisions advisory-only | P2/P3 | **Deferred** — documented residual (needs registry change-notification plumbing; not a correctness loss today) |
| Ops-C2/C3/C4 | journal byte-unbounded decisions; repair planner lacks live availability; planner/repair table drift | P3 | **Deferred** — fail-closed or latent today |
| Exec-C2 | TaskCenterExecutor::poll reports pruned task as FAILED | P3 | **Deferred** |
| MCP-D7 | dispatchAndAwait blocks the MCP loop up to 30 min (interaction tools) | P2 latent (headless hides GUI tools) | **Deferred** |

## Dedupe vs merged PRs

#1477/#1480/#1484/#1500/#1501 did NOT touch mcp_server.cpp, mcp_bridge.ts, exp-rs-spatial.ts, or tool_catalog/ (verified by `git show --stat` of each merge) — the recent fixes on this surface landed via #1469–#1471/#1346. No double-fixing occurred.

## Dedupe vs open PRs

- #1504 (geodata): disjoint from track C ownership. No overlap.
- #1505 (r13-j): owns `src/python/isolated/python_worker_process_pool.{cpp,h}` + `python_plugin_adapter.cpp`. Track C treats those as read-only. No C-side edit collides.
