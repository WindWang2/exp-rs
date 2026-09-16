# EVIDENCE — linked-visual-analytics-11

Append-only log of executed commands + outcomes. (Claims come only from here.)

## Phase 0 (main checkout, read-only)
- git fetch origin --prune → OK; origin/master=a5b11b7f10fa010c1c060864fb427d777ba9a4aa
- git log -20 --oneline origin/master; git branch -r → recorded in BASELINE.md
- gh pr list (2 open: #1009, #1008); gh pr diff --name-only both → recorded in PARALLEL_OWNERSHIP.md
- gh issue list (7 open: #1001–#1007) → dedupe in BASELINE.md
- ISSUES.md / CHANGELOG.md / docs/agents/goal-template.md read → no live VA items
- Subagent #1 (Explore, read-only) architecture audit → facts in BASELINE/CURRENT_ARCHITECTURE
- git worktree add ../exp-rs-linked-visual-analytics-11 -b zcode/linked-visual-analytics-11 origin/master → OK, HEAD=a5b11b7f

## Phase 6a — build (worktree-local, fresh configure)
- configure: cmake -S . -B build-dev -G Ninja (Debug, ENABLE_TESTS=ON,
  FETCHCONTENT_SOURCE_DIR_CATCH2=C:/deps/catch2-src, Qt 6.8.0 msvc2022_64 +
  vcpkg toolchain; winflexbison on PATH) → OK (1392.8 s configure).
  First attempt failed: FetchContent tried to clone Catch2 from GitHub
  (offline host) — resolved with the local v3.7.1 source dir, NOT by editing
  CMakeLists.
- build: cmake --build build-dev --target test_view_link test_visual_analytics
  with CMAKE_BUILD_PARALLEL_LEVEL=2. Fresh build-dev → full dep graph (3099
  steps). Resource samples: freeRAM 1.2 GB, cl.exe RSS ≈ 126–474 MB (2
  workers) — within budget, -j2 held; drop to -j1 only on OOM (none seen).

## Phase 6b — targeted gates (final binaries, committed state)
Environment fix discovered en route: the vendored-QGIS test binaries hung
invisible at process init — a hard-error dialog for a missing DLL
(qca-qt6.dll / qt6keychain.dll, 0xC0000135 via cdb stack dump). Fix: add
C:\deps\qca-install\bin + C:\deps\kc-install\bin to PATH in run_tests.cmd
(local runner script, not committed). Controlled evidence that the hang was
environmental: the untouched test_dual_viewport_sync hung identically, and
even a no-canvas test case hung before Catch2's banner.

Validation pass 1 (20:14): test_view_link EXIT 0; test_visual_analytics
EXIT 0; test_dual_viewport_sync EXIT 0; test_command_contract_9 EXIT 0.
Validation pass 2 (20:15): test_view_link "All tests passed (84 assertions
in 10 test cases)"; test_visual_analytics "All tests passed (100056
assertions in 12 test cases)"; test_dual_viewport_sync EXIT 0;
test_command_contract_9 "All tests passed (204 assertions in 6 test cases)".
Oracle 6 (double validation) satisfied for all four lanes.

## OUT_OF_SCOPE findings (recorded per autonomy rules)
1. master a5b11b7f does not compile src/workflow/pipeline_run_coordinator.cpp
   on MSVC (missing <fcntl.h> for _O_WRONLY/_O_BINARY) — fixed in-track with
   a 1-line include (file owned by open PR #1009; rebase trivial).
2. master a5b11b7f does not compile src/agent/data_platform_tools.cpp on
   MSVC (unqualified BenchmarkService with no using-declaration) — fixed
   in-track with 1 added using-declaration (file owned by PR #1009).
3. master a5b11b7f cannot build the sicnu_geo_rs APP TARGET on MSVC:
   D17 defined sicnu::workflow::WorkflowDefinition twice (workflow_types.h
   std::string-based vs workflow_ir_v2.h QString-based, same namespace;
   workflow_ir_v2.h:112 comment defers "full rename / namespace split").
   CMake AUTOMOC merges the headers into one TU → C2011. Controlled proof:
   compiling origin/master's own main_window_view.cpp.obj (stashed-checkout
   A/B) reproduces the identical C2011. NOT fixed in-track (real fix is the
   deferred D17 namespace split; a drive-by would touch D17's API) —
   documented here and in PR_BODY. My changed app TUs compile clean until
   the D17 header collision point (command_defs.cpp.obj builds alone).
4. master's help↔registry gate (test_command_contract_9) was RED: 10
   registered commands (cartography.*, workbench.cartography/classifyStudio/
   georefDual/ir2Pipeline/operatorCatalog/visualAnalytics) had NO
   data/help/commands.json entries. Fixed in-track by appending the 10
   missing entries — gate now green (204 assertions).
