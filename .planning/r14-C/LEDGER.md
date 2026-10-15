# R14-C Ledger

Working log, newest entries last. Status vocab: FIXED / ALREADY_FIXED / NOT_APPLICABLE / BLOCKED_WITH_EVIDENCE.

| # | Time (UTC+8) | Event |
|---|---|---|
| 1 | 10-15 | Host degraded honestly: ZCode native subagents instead of `/teamwork-preview`+`/goal-loop` (not registered here). |
| 2 | 10-15 | Live state re-read: master=f72f0c117; open PRs #1504 (geodata) + #1505 (r13-j; owns python_worker_process_pool.* + python_plugin_adapter.cpp → C dedupe-only). 31 open issues triaged into ISSUE_REVIEW_MATRIX. |
| 3 | 10-15 | Worktree `hardening/r14-C-pi-mcp-workflow` created at f72f0c117 with git-meta flock; master checkout untouched. |
| 4 | 10-15 | Configure#1 failed (build.sh not executable; exit masked by pipe — caught and re-run). Configure#2 failed: Catch2 FetchContent clone through dead socks5 proxy. Configure#3 OK with local Catch2 source override (offline). |
| 5 | 10-15 | Background build of 16 track targets started under global flock, -j2. |
| 6 | 10-15 | #1499 (python_worker_process_pool.cpp compile break) initial read: file at master has balanced namespace closes; last commit 56418a418 (#1486 mutex fix). Compile-verify pending build. |
| 7 | 10-15 | Five parallel read-only audits completed (MCP/catalog/Pi; workflow/DAG/IPC/checkpoint; agent_loop/agent_ops/planner; plugins/sdk/security; task-cancel/e2e/perf). Full verdict matrix → ISSUE_REVIEW_MATRIX.md. |
| 8 | 10-15 | Selected 8 work packages: FIX-1 [P1] MCP workflow server-side cancel; FIX-2 [P2] #1448 Engine-2 terminal persist/lock; FIX-3 [P2] approval digest guard; FIX-4 [P2] pi bridge fastCrash reset + outbound cap; FIX-5/6 [P2] provider take-over + builtin shadowing; FIX-7 [P2] env deny-list + BSD PATH; FIX-8b [P3] E6003 typed. FIX-8a (agent_session listed-but-dead) BLOCKED_WITH_EVIDENCE (needs src/app/main.cpp wiring — track D). |
| 9 | 10-15 | All implementations + tests written (4 new C++ test binaries, 1 new agent_ops case, 2 new pi bridge tests). RED for FIX-4 verified via stash cycle: test1 "Missing expected rejection", test2 real production failure "keeps crashing (5 fast crashes)". GREEN run 1: pi suite 34/34. |
| 10 | 10-15 | .gitignore: added allow-list for .planning/r14-C/*.md (markdown-only, r13-i-infra precedent; #1504/#1505 verified not touching .gitignore). |
| 11 | 10-15 | Sibling tracks A/B verified live (worktrees exp-rs-r14-A/-b; shared build lock serializes all tracks). System headroom OK (load ~2.8/16 cores, 55G avail). |
| 12 | 10-15 | Core build 16 targets exit 0 (#1499 "master compile break" claim refuted for this lane). RED cycle: src/ stashed, 5 test targets rebuilt, all RED observed (FIX-7 `!true`, FIX-1 :261/:344, FIX-2 :154, FIX-5/6 :190/:219, FIX-3 :1133; controls passed). |
| 13 | 10-15 | GREEN: fixes restored, rebuilt; ctest r14_C batch 10/10 ×2; pi 34/34 ×2. |
| 14 | 10-15 | Independent review R1: P0=0 P1=0, P2×2 (FIX-4 byte-cap; ORACLES overstated) — both reworked: Buffer.byteLength + discriminating CJK test (RED via sed-swap verified), ORACLES rewritten with observed evidence only. pi post-rework 16/16 bridge, 35/35 full. Doc rework: external_process.h contract, CMake comment. |
| 15 | 10-15 | Regression suites (changed-code neighbors): exprs_ipc 28✓, exprs_external_process 13✓, plugins_runtime_host 7✓, surface_protocol_r4 11✓, workflow_checkpoint_cache 45✓, d17_e2e 9✓, agent_ops_core 47✓. test_mcp_server 29/30 — A/B on stashed base: SAME deterministic failure ⇒ pre-existing master defect (timing-dependent ==1 vs archive sweep live+history). |
| 16 | 10-15 | FIX-9 (test-only, shared test_mcp_server.cpp short-lease): rebind probe gets distinct pipeline id; contract now asserted as "no checkpoint for the refused id" + b.db absent — timing-free. Rebuilt: 30/30 (9670 assertions). |
