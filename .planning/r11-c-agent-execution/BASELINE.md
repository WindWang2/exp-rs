# Direction C Baseline & Remote Reconnaissance

- **Worktree**: `/home/kevin/projects/rs-studio/exp-rs-r11-c-agent`
- **Branch**: `hardening/r11-c-agent-execution`
- **Base Commit**: `8a08642760e2e8ffd2c4b965fc4683c01d3e4758` (`origin/master` at 2026-10-13)
- **Host**: Linux 6.8.0-60-generic x86_64, Qt 6.12.0, Python 3.14.7, Ninja 1.13.2, CMake 4.4.4
- **Parallel Limit**: `CMAKE_BUILD_PARALLEL_LEVEL=2`, `CTEST_PARALLEL_LEVEL=1`
- **Protocols Status**:
  - `/teamwork-preview`: `NOT_AVAILABLE` as interactive slash command in host session; mapped to Antigravity native 3-subagent role separation (Scout read-only, Implementer, Independent Reviewer).
  - `/goal-loop`: `NOT_AVAILABLE` as installed skill; executed via strict Oracle → Red → Green × 2 → Ledger cycle.

## Remote Reconnaissance Summary

### Git Remote & Master Log
- Remote origin: `https://github.com/WindWang2/exp-rs.git`
- `origin/master` HEAD: `8a08642760e2e8ffd2c4b965fc4683c01d3e4758`
- Recent 15 commits:
  - `8a08642760` Merge hardening/r6-digest-identity-convergence: MSVC configure unblock (WHOLE_ARCHIVE twin-item conflict)
  - `09a7ef5bd6` Merge rs14-unified-verifier: unified scientific verifier subsystem (RS14, ADR branch doc)
  - `84d782c408` Merge pull request #1474 from WindWang2/hardening/r8-ui-teaching-context
  - `78148cc006` Merge origin/master (PRs #1469-#1473) into hardening/r8-ui-teaching-context
  - `cec4dc4491` Merge pull request #1473 from WindWang2/hardening/r8-data-experiment-integrity
  - `f0e44192a6` Merge pull request #1472 from WindWang2/hardening/r8-science-algorithm-closure
  - `57816eebb9` Merge origin/master (PR #1469) into hardening/r8-science-algorithm-closure
  - `107e2b17d7` Merge pull request #1471 from WindWang2/hardening/r8-runtime-boundaries
  - `741bd54227` Merge origin/master (PR #1469) into hardening/r8-runtime-boundaries
  - `87806e90e0` Merge pull request #1470 from WindWang2/hardening/r8-agentops-workflow-closure
  - `35e644c31d` Merge origin/master (PR #1469) into hardening/r8-agentops-workflow-closure
  - `428dcef4ea` Merge pull request #1469 from WindWang2/fix/review-p0-p1-wave
  - `569ce67293` fix(tests): final adversarial-review round — close the three tip-red paths
  - `3112cfb94c` chore(app): remove rs_job_runner — dead code with a latent callback race (#1453)
  - `b03cd066bb` fix(app): UI state honesty — workflow gating, fail-closed affordances, registry menu projection; + adversarial-review fixes (#1453/WP02/WP06)

### Open PRs (Audit Snapshot)
1. **PR #1478**: `hardening/r10-ui-teaching-help-consistency` (UI, teaching admin, help integrity)
2. **PR #1477**: `hardening/r9-agent-execution-reliability` (Agent runtime, IPC, TaskCenter catalog lifetime, Python worker double handout, ModelRuntime session coalescing)
3. **PR #1476**: `hardening/r9-ui-teaching-test-quality` (UI, scan pool, teaching admin script adapters)
4. **PR #1475**: `hardening/r9-science-data-integrity` (Science/data integrity, resolver, radiometric state, fabric query planner, model_catalog)

### Unresolved / Residuals from PR #1477
- PR #1477 touched `src/sdk/exprs/ipc_channel.*`, `src/sdk/exprs/external_process.*`, `src/sdk/exprs/plugin_registry.*`, `src/operators/runtime/model_runtime.*`, `src/processing/framework/task_center.*`, `src/python/isolated/python_worker_process_pool.*`.
- Documented residuals in #1477:
  1. `PythonWorkerProcessPool`: `isRestarting`, `crashBudgetLeft`, `restartCount` are written outside node mutex.
  2. `test_execution_plane_9`: `WorkflowRunCoordinator::onTaskUpdated`'s `releaseRunOwnership` (worker thread) vs `drainPersists()` (main thread) lacks happens-before.
  3. Linux/macOS and physical GPU not verified in PR #1477 (built on Windows MSVC). We are on Linux, so Linux tests provide essential validation.
