# R14-C Oracles — per work package

Legend: FIXED (this track, with RED→GREEN×2 evidence), FIXED_ON_MASTER (upstream, cited commit), DEFERRED (documented residual), BLOCKED_WITH_EVIDENCE, NOT_APPLICABLE.

| WP | Oracle | Verdict |
|---|---|---|
| 01 | Intent→tool-execution flow map with failure matrix | DONE — audit report (A) section: Pi→bridge→--mcp entry→StdinReader→handleRequest→catalog projection→dispatch chain→typed errors; recorded in ISSUE_REVIEW_MATRIX + PR |
| 02 | Invalid/oversized/illegal JSON never crashes or waits forever; error codes reproducible | Largely FIXED_ON_MASTER (#701 caps, -32700/-32600/-32002/-32602/-32601); **FIX-1** adds the missing cancel contract for run_workflow; DEFERRED: McpToolError::rpcCode dead field, meta-tool argument coercion (P3) |
| 03 | Fake-subprocess abort/partial frame/restart returns accurately, never hangs | Mostly FIXED_ON_MASTER (F-PI-1/F-PI-2 tests); **FIX-4** closes fastCrash-count latch + outbound cap; DEFERRED: Pi tool set frozen at extension load (P3) |
| 04 | Every listed tool callable or explicitly unavailable | Partial: meta/data-platform rows probe-tested; `scientific:agent_session` listed-but-dead = **BLOCKED_WITH_EVIDENCE** (needs track-D main.cpp wiring or snapshot-wide relisting); catalog staleness on late registrations DEFERRED (P2, no correctness loss — dispatch resolves live) |
| 05 | Second plugin cannot hijack first plugin's tools; no dangling objects after unload | Plugin-vs-plugin FIXED_ON_MASTER (e0664f65f); **FIX-5** (provider take-over) + **FIX-6** (builtin shadowing) close the remaining halves; releaseAll cross-impact on unload DEFERRED (P3, safe direction) |
| 06 | Bounded replay memory; every request ends in success or exactly one typed failure | FIXED_ON_MASTER (#1480 bounded queues, E6008, mid-frame resume); **FIX-8b** removes substring-keyed E6003 classification; unknown-method FIXED (7652b1190) |
| 07 | 1000× failure injection: no zombie/socket leak/busy-loop/UAF | Pool core FIXED_ON_MASTER (1f4062bf2, 56418a418, ed3b1cb6c) + #1505 in flight (dedupe); residual `m_pendingRecovery` off-GUI-thread mutation + unload QEventLoop re-entrancy remain in #1505-owned files → reported for the owner, not duplicated |
| 08 | Bad DAG rejected before execution; legal DAG deterministic | FIXED_ON_MASTER (IR2 fail-closed parse, 3-color cycle DFS, in-degree invariants; 24-case suite); residual: mixed-input $ref typo not validated at plan time (P3, DEFERRED) |
| 09 | cancel/complete race → exactly one terminal outcome; callbacks never lost | Engine-1 FIXED_ON_MASTER (c8c463d22 + absorbing terminal states; 20-iteration race test); **FIX-1** extends the contract to the MCP surface for workflows; D17 cancel→Cancelled fold verified |
| 10 | kill + resume never replays completed side effects or writes failure as completion | Engine-1 FIXED_ON_MASTER (d5ab7bc0b, digest re-verification, impl-stamp gates); Engine-2 gap **FIX-2** (terminal persist failure retains flock); torn-write/archive-swap FIXED (e7e4f77a0) |
| 11 | Bounded journal keeps decisions, evidence, recovery-critical records | FIXED_ON_MASTER (f2e492087 goal pin + decision-preserving compaction); DEFERRED: entry-count bound vs byte blow-up via oversized decision payloads (P3, fail-closed write refusal exists) |
| 12 | No cwd escape, PATH hijack, or fail-marked-as-success; typed denials explain | #1460 four claims FIXED_ON_MASTER; **FIX-7** adds loader-injection env deny-list (typed E5007) + BSD probe/exec PATH consistency; TOCTOU residue documented as PATH-policy-not-sandbox (accepted) |
| 13 | Same input + candidates → stable output; unavailable tools never recommended | FIXED_ON_MASTER (#1500: stable_sort total keys, canonical fingerprints, mirror Available-gating); DEFERRED: repair-side live-availability filter + planner/repair table drift check (P3) |
| 14 | Same params → same results across GUI/CLI/Pi; no boundary bypass | VERIFIED — one projection (collectSurfaceTools), one dispatch policy (surfaceIdAllowed), one execution chain (TaskCenter/coordinator); parity suites green |
| 15 | Measured before/after peaks and P95; no new races under load | Scope honest-downscoped: no wall-clock MCP-surface benchmark existed at HEAD and none was added this round (perf DEFERRED with inventory recorded — logical-scale gates exist: test_surface_parity:433, test_catalog_size) |

## RED → GREEN evidence index

All runs on build-r14-C (Ninja, Debug, -j2 under /tmp/exp-rs-r14-global-build.lock, QT_QPA_PLATFORM=offscreen, ctest -j1), 2026-10-15. RED = production fixes stashed (`git stash push -- src/`), test targets rebuilt, tests run; GREEN = fixes restored, rebuilt, run twice.

| Fix | RED (unfixed code, observed) | GREEN×2 (observed) |
|---|---|---|
| FIX-1 | test_r14_C_mcp_workflow_cancel.cpp:261 & :344 FAILED (cancel notification no-op; run kept burning) | 10/10 r14_C ctest batch ×2 (incl. both FIX-1 cases + hygiene control) |
| FIX-2 | test_r14_C_d17_terminal_persist.cpp:154 FAILED (peer resume SUCCEEDED — ownership had been released); control passed | same batch ×2 |
| FIX-3 | test_agent_ops_core.cpp:1133 FAILED (Json::LogicError escaped run()) | same batch ×2 |
| FIX-4 | pi/mcp_bridge.test.mjs: "Missing expected rejection" + real "keeps crashing (5 fast crashes)" production failure | pi suite 34/34 ×2 pre-rework; post byte-correctness rework 15/15 + full 35/35 (16 bridge tests) |
| FIX-4 rework (byte cap) | sed-swap byteLength→.length: byte test FAILS (16 tests, 1 fail) | restored: 16/16 |
| FIX-5/6 | test_r14_C_plugin_registration.cpp:190 & :219 FAILED (take-over/shadowing accepted) | same 10/10 batch ×2 |
| FIX-7 | test_r14_C_external_process_env.cpp:33 `REQUIRE_FALSE(result.started)` → `!true` (spawn proceeded with LD_PRELOAD); benign control passed | same 10/10 batch ×2 |

Exact ctest invocation: `ctest --test-dir build-r14-C -R 'R14-C FIX|malformed driver-held repair approval' -j1 --output-on-failure` → run1 "100% tests passed, 0 tests failed out of 10" (5.94s), run2 same (3.36s). pi: `node --test pi/test/` → 35/35.
