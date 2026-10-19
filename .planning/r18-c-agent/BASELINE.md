# R18-C Baseline (LIVE recon 2026-10-19)

## Repository state
- `origin/master` = `2d2881631fc821537100c00da677888e3a31ea8f` (Merge PR #1533, r17-G)
- Task snapshot SHA `7825c1ebd` was **stale** at execution time (ancestor of above); all work is based on the LIVE SHA.
- Worktree: `/home/kevin/project/exp-rs-r18-c-agent`, branch `agent/r18-c-pi-mcp-workflow`, created via `scripts/dev/new_worktree.py --base origin/master`. First attempt failed (network blob fetch); retry succeeded.
- Local master checkout `/home/kevin/project/exp-rs` is 149 commits BEHIND origin/master; all read-audit findings were re-verified against this worktree before being acted on.

## Open PRs / issues (at recon time)
- Open PRs: **#1513** only (draft, `hardening/r15-B-geodata-persistence`). Write set: `src/cli/**`, `src/data/**`, `src/dataset/**`, `src/experiment/**`, `src/geospatial/**`, 6 test files, `tests/CMakeLists.txt`. Disjoint from R18-C zones except `tests/CMakeLists.txt`.
- Open issues: #1491 (CI infra, no completed runs since 2026-10-11 — not agent code), #1467 (core-fork scene-read/NaN cluster — other tracks). Neither is owned by R18-C.
- Unmerged remote branches: only `origin/hardening/r15-B-geodata-persistence`.

## Recently merged touching this track's zones
- #1507 r14-C `hardening/r14-C-pi-mcp-workflow` (Pi bridge + mcp_server + workflow cancel)
- #1516 r15-D (Pi bridge failed-start generations, StdinReader EOF/badbit, Windows junction, workflow non-object gates, agent_loop seams, plugin capabilities)
- #1517/#1523 r15-E/r16-E (help/i18n + `pi/mcp_bridge.ts`, `src/agent/mcp_server.cpp`, `session_seams`, `workflow_checkpoint` follow-ups)
- #1518 r16-B (science_context broker/agent_adapter)
- #1519/#1521 r16-C/r16-A (science-context consumers)
- #1503/#1512/#1524 Qt/QGIS/cartography (R18-D lineage)

## Sibling local worktrees (unmerged, NOT pushed — avoid their write sets)
- `hardening/planner-repair-capability-graph` @5f06f6ce: owns `src/preflight/finding.*`, `src/repair_planner/repair_schema.*`, `tests/test_preflight_report_schema.cpp`, `tests/test_repair_planner_schema.cpp`, root `CMakeLists.txt`.
- `hardening/verifier-grader-explain-evidence` @f3aaeb2c: owns `src/verify/**`, `src/grader/**`, `src/explain/**`, `tests/test_{evidence_seam,explain_schema,grader_schema,verifier_schema}.cpp`.

## Overlap scans (scripts/dev/overlap_scan.py, evidence-only)
- `--include-merged 40` over the 12 core R18-C files: 0 open PRs, 0 remote branches, 0 sibling-worktree *uncommitted* hits; 9 merged PRs (r11-c … r16-c) form the do-not-duplicate fix inventory.
- `narrow_targets.py src/workflow src/agent_loop --base origin/master`: empty (no local changes yet).

## Build / test baseline
- Build dir: `build-r18-c` (independent `cmake -S . -B`), `-DCMAKE_BUILD_TYPE=Debug -DENABLE_TESTS=ON -DENABLE_LOCAL_BUILD_SHORTCUTS=ON`.
- Parallelism: `-j2` hard cap (repo docs prefer -j1, allow -j2; host has 40c/62G but five tracks share it). Global host lock `/tmp/exp-rs-five-tracks-build.lock` + `resource_guard.py --max-parallel 2`.
- Baseline test results: see TEST_RESULTS.md (filled as stages complete).

## Deep-read evidence (three read-only audits + re-verification in this worktree)
Candidate defect inventory carried into staged work (all re-verified against `2d2881631` before fixing):
- Pi bridge: timeout-without-server-cancel; hung-startup zombie state; category-case asymmetry; silent empty-schema fallback; wait-tool deadline off-by-one/blind JSON; protocol-version negotiation ignored; stop-during-startup pending initialize.
- MCP server: progress unit mismatch (0..1 vs 0..100) — test-masked; `inputSchema` vs `schema` key drift across discovery surfaces; cancel-map id namespace collision (number vs string); limit clamp drift; `handleAgentSession` SEAMS_UNAVAILABLE reported with isError:false; containment false-positives on non-path strings.
- Workflow/agent_loop: `Cancelling→Completed` illegal-but-forced at 3 sites; premature `Cancelling→Canceled` step fold (verdict flap); hard-coded 30-min plane await; `TaskCenterExecutor` MULTI_STEP_PLAN_UNSUPPORTED blocking the real planner chain (C10); broker cache digest gaps; journal compaction vs 1 MiB contract; `findStepPlan` lock escape.
Full audit reports with file:line are summarized in TASKS.md per stage.
