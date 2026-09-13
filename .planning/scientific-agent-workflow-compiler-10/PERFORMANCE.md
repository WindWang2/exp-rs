# EVIDENCE — scientific-agent-workflow-compiler-10
Every capability claim maps to a local command + exit code, or is marked not-executed.

## Phase 0
- `git fetch --all --prune && git pull --ff-only` → "已经是最新的" (exit 0)
- `git rev-parse origin/master` → 7d78059d1a6d316d606656759a506d17bc5e3b55
- `git worktree add ../exp-rs-scientific-agent-workflow-compiler-10 -b zcode/scientific-agent-workflow-compiler-10 origin/master` → exit 0
- `git check-ignore .planning/scientific-agent-workflow-compiler-10/GOAL.md` → no output, exit 1 (tracked); `-v` shows negation pattern `.gitignore:149`
- Sibling overlap check: `git ls-tree -r origin/zcode/temporal-eo-phenology-change-10 | grep docs/adr` → 0148 claimed; #975/#974 no harness paths touched (verified via `git diff --name-only master...origin/<b> | grep -c harness/` → 0 for both)
- gh pr list (30) → dedupe table in BASELINE.md

## OUT_OF_SCOPE
- F-OPS-3 (rs:qa_mask fail-open), F-OPS-4 (io:reproject srcCrsOverride dead param): src/operators — algorithms track. Recorded as constraints: repair rules avoid depending on either path (pinned by tests in WP3).
- #869–#871 help-store drift: unified-help-diagnostics-6 lane.

## Phases 1-5 (implementation)

- All new sources pass `-fsyntax-only` against the real build flag set
  (worktree build-dev compile_commands) BEFORE the full build: 6/6 modules +
  6/6 test files OK (two rounds; every round's errors fixed to zero).
- Full build: `cmake --preset dev-default -DCMAKE_CXX_COMPILER_LAUNCHER=ccache`
  (exit 0) then `cmake --build build-dev -j2 --target <suites>` → BUILD_EXIT=0
  (CMAKE_BUILD_PARALLEL_LEVEL=2 exported; -j1 not needed: RSS/load within
  policy; a transient ENOSPC during libqgis_gui link was host disk pressure,
  resolved by the failed link freeing its temp files, then the same command
  completed).
- Pi suite: `cd pi && node --test test/` → 9/9 PASS (5 pre-existing lifecycle
  tests + 4 new drift/F-PI-1/F-PI-2 tests, including a behavioral flood-recovery
  test that fails on the pre-fix code).

## Phase 8 — final verification (at final HEAD b19d7da96d+)

- `git diff --check 7d78059d1a..HEAD` → clean (exit 0).
- Conflict-marker scan (<<<<<<< / >>>>>>> / =======) over src/agent/harness, pi,
  tests → none.
- Secret scan over changed files → only prose matches ("token budget");
  no credentials.
- Existence assertions (goal-template §存在性断言): goal-template.md,
  loop-template.md, command-vocabulary.md, .agents/AGENTS.md, CLAUDE.md → all
  present. Skill references in planning docs limited to existing repo skills.
- Test suites (serial, QT_QPA_PLATFORM=offscreen, cwd build-dev):

| Suite | Result |
|---|---|
| test_workflow_ir (new) | 89 assertions / 8 cases PASS |
| test_workflow_analysis (new) | 125 / 18 PASS |
| test_workflow_repair (new) | 102 / 9 PASS |
| test_workflow_planner (new) | 50 / 6 PASS |
| test_context_checkpoint (new) | 75 / 6 PASS |
| test_tool_shortlist (new) | 40 / 7 PASS |
| test_harness_error | 42 / 4 PASS |
| test_harness_eval_corpus (+3 categories) | 756 / 2 PASS |
| test_harness9_contracts | 289 / 11 PASS |
| test_harness_grounding | 126 / 7 PASS |
| test_harness_evidence | 58 / 7 PASS |
| test_harness_evals | 269 / 17 PASS |
| test_agent_tools_3 | 126 / 6 PASS |
| test_spatial_contracts | 67 / 9 PASS |
| pi/test (node --test) | 9/9 PASS |

- Pre-existing master failures (verified present at baseline 7d78059d1a, NOT
  introduced here; documented, not fixed — generated-data lanes):
  test_capability_drift ×3 (rs:gaofen/zy3/hj_import + cartography:
  diff_templates/explain/export uncovered; harness.optical_ndvi_landsat alias
  surfacing).
- `git rebase` not needed: origin/master unchanged at 7d78059d1a (verified
  `git rev-list --count HEAD..origin/master` → 0).
