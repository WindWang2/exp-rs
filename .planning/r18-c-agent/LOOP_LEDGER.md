# R18-C Loop ledger

Format: `# | hypothesis | changed files | verify command | exit code | result | next`

## Iterations

(000) 2026-10-19 | LIVE recon hypothesis: snapshot SHA stale, open-PR set changed | — (read-only) | preflight/overlap/stale scans + gh queries | 0 | master=2d2881631; only #1513 open; two sibling local worktrees fenced | create worktree

(001) 2026-10-19 | worktree creation needs one retry (blob:none network flake) | — | new_worktree.py --base origin/master | 0 (2nd try) | branch agent/r18-c-pi-mcp-workflow @2d2881631 | configure build

(002) 2026-10-19 | deep-read via 3 read-only auditors, findings re-verified per DECISIONS D4 | — (read-only) | agent audits + worktree grep | 0 | candidate inventory recorded in TASKS.md | baseline build+tests

## C00 — merge-after-gap restoration of #1516 (r15-D) content silently reverted by merge 29596c913 (PR #1517)

Forensics: merge 29596c913 (r15-E "merge master") resolved ALL conflicted files to the r15-E side (pre-r15-D). r15-E's branch had ZERO own changes to these files (verified vs merge-base f326d8ca9); the discard was pure conflict-resolution error. No commit after 8eded4a68 (#1517 merge) touched any of the 19 affected files → restore is collision-free. Full REVERTED set: pi/{mcp_bridge.ts,test/*}(5), src/agent/mcp_server.cpp, src/agent/tool_catalog/workspace_containment.h, src/agent_loop/session_seams.cpp, src/agent_ops/task_center_executor.cpp, src/plugins/host/plugin_host_worker_main.cpp, src/sdk/exprs/plugin_capabilities.h, src/workflow/{workflow_checkpoint,workflow_definition}.cpp, tests/{test_agent_loop_seams,test_agent_ops_task_center_executor,test_mcp_server,test_plugin_capabilities,test_workflow_execution_plane,test_workflow_recovery}.cpp.

(003) 2026-10-19 | H: r15-D pi regression tests fail against reverted bridge | restore pi/test/* only | `node --test pi/test/*.test.mjs` | 1 | RED: 4 fail — "spawn failure leaves bridge dead", "initialize timeout tears hung child + respawn", "stream desync teardown", "runaway line anchor"; 33 pass | restore bridge

(004) 2026-10-19 | H: restoring bridge flips RED→GREEN | + pi/mcp_bridge.ts (from a8d5cd510) | same command ×2 | 0,0 | GREEN×2: 37/37 pass both runs (was 35 tests pre-restore; +2 restored) | C++ half

(005) 2026-10-19 | FALSE POSITIVE: broker cache digest gap claim | — (read-only) | code read broker.cpp:221-280 | 0 | `observed` is built one-per-passport in the same loop order → index alignment holds by construction; digest covers all passports | close C06 sub-item, no patch

## C05 — cancel/timeout symmetry (pi)

(006) 2026-10-19 | H: request timeout leaves server-side work running (abort sends notifications/cancelled, timeout does not) | pi/test/fake_mcp_server.mjs (+--hang-tool-calls mode), pi/test/r18_c_timeout_cancels_server_side.test.mjs, pi/mcp_bridge.ts (requestTimeoutMs plumbing, behavior-identical) | `node --test pi/test/r18_c_timeout_cancels_server_side.test.mjs` | 1 | RED: "expected one cancel, got []" — zero notifications/cancelled after timeout | apply fix

(007) 2026-10-19 | H: notify on timeout flips RED→GREEN | +4 lines in mcp_bridge.ts timer callback | same ×2, then full suite | 0,0 / 0 | GREEN×2 (1/1); full pi suite 38/38 exit 0 | C05-a done

## C01 — surface authority

(008) 2026-10-19 | H: tools/list silently rewinds garbage cursors to page 0 | tests/test_r18_c_surface_pagination.cpp (3 cursor sections + full-surface schema parity pin), tests/CMakeLists.txt (+1 target block) | pending build | — | test written; fix staged (typed -32602 for non-numeric cursor) | build + RED/GREEN once toolchain run lands

(008b) 2026-10-19 | toolchain: 5 configure iterations (GDAL→PROJ→GSL→NETCDF→Catch2 offline; expat/sqlite/zlib/zstd must be SYSTEM .so — vendor .a are non-PIC and vendor .so symlinks dangling; vendor libgdal.so needs wrapper2_* LAPACK symbols nobody provides → local-only `-DCMAKE_EXE_LINKER_FLAGS=-Wl,--allow-shlib-undefined`, lazy PLT, tests never call that path). Runtime: `LD_LIBRARY_PATH=/usr/lib/pulseaudio:/usr/lib:<pw-seismic>/x86_64-linux-gnu:<pw-seismic>/usr/lib` (vendor prefixes must NOT shadow system Qt6.12/pulse). Full recipe in DECISIONS D11/D12. | — | configure/build/ldd | 0 | qgis_core + test_mcp_server + test_agent_loop_seams linked | proceed

## C00 C++ RED evidence (reverted sources, restored tests)

(009) 2026-10-19 | RED run 1: `test_mcp_server "[stdio]"` | restored r15-D tests vs reverted src | exit code path (catch2) | FAILED | "StdinReader treats clean EOF as end-of-input, not an overlong line" — 1 case, 5 assertions failed (clean EOF answered with bogus −32700 sentinel; exact-4MiB boundary frame rejected) | restore src

(010) 2026-10-19 | RED run 2: `test_agent_loop_seams` | same | FAILED | 10 cases, 1 failed: "data state snapshot refuses wrong-typed asset leaves (fail closed)" (stringLeaf guard reverted) | restore src

(011) 2026-10-19 | RED run 3 (compile-level): `test_plugin_capabilities` fails to COMPILE — `writeRootGateActive` not declared (symbol introduced by reverted 75aef619b fix) | build log | FAILED | compile-level proof of the revert | restore src

(012) 2026-10-19 | C03-a progress RED deferred to its own test binary (see 015) | | | | |


(013) 2026-10-19 | C00 C++ restore + re-apply own fixes (stash-pop failed → patch re-apply) | 8 src files restored verbatim; cursor/cancel fixes re-applied | rebuild | 0 | GREEN R1: mcp 9686/31, seams 101/10, agent_ops 101/28, plane 96/8, recovery 163/15, plugin 129/14 | C01 parity sweep

(014) 2026-10-19 | H: tools/list vs get_tool_schema drift exists | test_r18_c_surface_pagination parity sweep | run | 42 | RED: 11 mismatches | fix authority

(015) 2026-10-19 | H: dedicated RED for C01/C03-a/C05-b | fixes reverse-applied → rebuild 3 targets | run | 42×3 | RED: progress 2 failed, cancel 2 failed, surface 4 failed | re-apply fixes

(016) 2026-10-19 | H: final GREEN ×2 all 12 suites | full rebuild + 2 rounds | runs | 0×24 | ALL GREEN (~17k assertions ×2) | commit series

(017) 2026-10-19 | atomic commit series D14; commit-1 initially missed mcp_server.cpp → detected, split via hunk patches, history rebuilt (soft reset) | 9 commits | git log | 0 | clean series | rebase

(018) 2026-10-19 | rebase a60c8831c (#1513 merged mid-run) | — | git rebase | 0 | zero conflicts | rebuild+review

(019) 2026-10-19 | post-rebase full rebuild | 13 targets | build | 0 | all linked | R1 review

(020) 2026-10-19 | R1 independent review | — (read-only) | agent audit | 0 | READY; P1 get_tool_schema metadata capture, P2 cursor inconsistency ×2 | fix P1+P2

(021) 2026-10-19 | R1 fixes: catalog branch keeps rich metadata; mcpTypedCursor ×4 sites + new pin | src/mcp_server + test | build+run | 0 | 1125/3, 9685/31, 5484/12, 74/10 | commit 7598a5d80

(022) 2026-10-19 | R2 independent review (falsification attempts V1–V7 all PASS) | — | agent audit | 0 | READY; F1 key injectivity, F2 residual cursors, F3 malformed parity, F5 comment | adopt F1/F2/F3/F5

(023) 2026-10-19 | R2 follow-ups: 'g',17 key; search_algorithms cursor; malformed-reg raw fallback; comment | src/mcp_server | build+run | 0 | 1125/3, 6/1, 9686/31, 5484/12 | commit e3a2d4c9c

(024) 2026-10-19 | final pi confirmation | node --test ×1 | 0 | 39/39 | docs + review_pack + PR
