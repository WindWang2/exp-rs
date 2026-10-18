# R16-D Issue / PR / Branch Matrix (executed 2026-10-17)

Baseline: `origin/master` = `46791bc65c6f552ebbcf723bb91892736d56f319` (matches task reference; NOT treated as permanent).

## Open PRs (5, live re-check — task brief snapshot said 4)

| PR | Branch | Files vs R16-D tracks | Action |
|----|--------|----------------------|--------|
| #1513 (Draft) | r15-B geodata | src/{cli,data,dataset,experiment,geospatial} | zero overlap with T07/T08 → read-only |
| #1514 | r15-A science | src/{analysis,operators/rs,processing/algorithms} | zero overlap → read-only |
| #1515 | r15-C model runtime | src/operators/runtime, src/runtime/chunk, tests/CMakeLists.txt (append-only) | zero overlap; we extend existing registered test files instead of appending |
| #1516 | r15-D pi/mcp | pi/**, src/{agent(mcp_server,tool_catalog),agent_loop,agent_ops,plugins,sdk,workflow} | zero overlap with cartography/mapspec → read-only |
| #1517 | r15-E desktop/teaching/help | **src/app/shell/{rs_job_panel,task_panel_host,task_progress_text,workflow_session_controller}, src/app/widgets/rs_result_summary.cpp, src/app/teaching_admin, src/teaching, src/help, src/lab_pack, data/help** | **T07 must NOT touch these files.** ribbons: `src/app/shell/ribbon_controller.cpp` is NOT in #1517's diff — verified via `gh pr diff 1517 --name-only`. T07 work on ribbon_controller is allowed; keep it disjoint from #1517's four shell files. |

## Open Issues (25) — triage against R16-D scope

| Issue | Area | Status vs R16-D |
|-------|------|-----------------|
| #1499 python_worker_process_pool compile break | src/agent_loop | NOT ours (r15-D #1516 domain); not reproduced in our build (python off) |
| #1495 ci clang-tidy continue-on-error | CI | not owned (no CI writes this round) |
| #1494 orphan test test_challenger_m1_2_harness | tests | not in T07/T08 test families; left for owner track |
| #1491/#1490 CI concurrency | CI | not owned |
| #1489 lupdate scans only src/app | i18n | T08 files live in src/agent → outside scan; fixing i18n config not in our file ownership; recorded, no action |
| #1488 canonical_json INT64 | verification | not ours |
| #1486/#1485/#1462 python/SSRF/plugins | agent/python | not ours |
| #1468 Windows _exit / #1466 suitability goal types | teaching/cluster | already fixed per task brief verification (tests/sicnu_test_main.cpp normal return; suitability_goal.cpp failure path present) — no action |
| #1467 core-fork NaN composites | core-fork | not ours |
| #1465 help/science_context | help | #1517 domain |
| #1464 verify/grader | verification | not ours |
| #1463 agent_ops journal | agent_ops | not ours |
| #1460 cli/sdk | cli | not ours |
| #1459 workflow/runtime | workflow | #1516 domain |
| #1458 experiment portable bundles | experiment | #1513 domain |
| #1456/#1455 geospatial egress/operators | geospatial | not ours |
| #1454 processing task-center | processing | not ours |
| #1449 sqlite destruction windows | stores | not ours |
| #1448 stores fail-open | stores | not ours |
| #1447 jsoncpp untyped access 9 sites | json | **type class matches T08-F03** (chart_registry asString); our fix covers cartography/chart sites only; issue scope broader → do not close |

**No open issue is directly owned by T07/T08.** Both tracks are audit-driven (fresh findings, evidence in WORK_PACKAGES.md). Issues are NOT closed by this PR.

## Remote branches / local worktrees

22 remote branches; local worktrees verified via `git worktree list --porcelain`:
- r16-a (integration + t01 + t02), r16-b (integration + t03 + t04), r16-c (integration + t05 + t06) — other concurrent prompts, untouched.
- Legacy: hardening/planner-repair-capability-graph, verifier-grader-explain-evidence, agent/cpp-build-run-13, ds41-http-fetch-strict, rs14-experiment-debugger — untouched, not reset, not cleaned.
- R16-D creates only: `hardening/r16-d-t07-workbench`, `hardening/r16-d-t08-cartography`, `hardening/r16-d-workbench-cartography` (+ worktrees `exp-rs-r16-d-t07`, `exp-rs-r16-d-t08`, `exp-rs-r16-d-integration`).

## File conflict matrix (R16-D vs open PRs)

- T07 write set: `src/app/main_window_vector.cpp`, `src/app/main_window.h` (lease member), `src/app/display/qgis_display_manager.{cpp,h}`, `src/app/display/qgs_display_stretch.cpp`, `src/app/display/piecewise_linear_enhancement.{cpp,h}`, `src/app/shell/ribbon_controller.cpp`, `src/app/workbench/agent_context_tool.cpp`(+ doc), `src/app/main_window_workbench.cpp` (one lambda), tests: `test_r14_D_vector_edit_honesty.cpp`, `test_qgis_display_manager.cpp`, `test_display_stretch.cpp`, `test_workbench_host.cpp` (maybe). → intersection with #1517 = ∅ (verified file-by-file). Intersection with #1513/14/15/16 = ∅.
- T08 write set: `src/agent/mapspec/{mapspec.cpp,mapspec_conditions.cpp,series_planner.cpp}`, `src/agent/cartography/{chart_registry.cpp,export.cpp,composition.cpp,quality.cpp,style_spec.cpp,style_compiler.cpp}`, tests: `test_mapspec.cpp`, `test_cartography_production_11.cpp`, `test_cartography_quality.cpp`, `test_platform5.cpp` (existing registered files only). → intersection with all open PRs = ∅.
- Shared mutable: `tests/CMakeLists.txt` — **no changes planned** (extend existing registered test files only).
