# R13-H Baseline — Scientific Planning, Suitability Routing & Repair Decision Chain Quality Convergence

## Snapshot (2026-10-14, Host Linux)

- Repository: `WindWang2/exp-rs`
- Default Branch: `master`
- BASE_SHA: `892295685e3606f3ac54c959954dde626508cc17` (Merge pull request #1497)
- Worktree: `/home/kevin/projects/rs-studio/exp-rs-r13-h-scientific-planner`
- Branch: `hardening/r13-h-scientific-planning` (tracked from `origin/master`)
- Build directory: `build-r13-h` (Ninja, Debug, -DENABLE_TESTS=ON)

## Merge Train Context

Recent merged PRs on master:
- #1497: `hardening(r12-e): verification, grading, faultlab & evidence-chain hardening`
- #1496: `hardening(r12-f): 既有模型算子运行时 OTB/IO 进程生命周期与内存账本硬化`
- #1484: `hardening(r12-c): harden agent workflow runtime, checkpoint durability, and memory budgets`
- #1483: `hardening(r12-a): remote sensing algorithm numerics, nan/inf safety, and test determinism`
- #1482: `hardening(r11-d): 存量 Qt 教学实验室与 QGIS 集成健壮性收敛`
- #1481: `fix(io): harden geospatial I/O and projection`
- #1480: `fix(agent,ipc,workflow): harden IPC channels`
- #1479: `hardening(r11-a): 存量遥感科学算法数值稳定性与确定性收敛`

## Active Open PRs and Worktrees

- Open PRs on GitHub: 0 open PRs.
- Active parallel tracks identified in worktree scan:
  - `/home/kevin/projects/rs-studio/exp-rs-r13-g-cartography` on `hardening/r13-g-cartography-mapspec` (R13-G)
  - Other legacy / completed worktrees.

## Open Issues Inventory & Status Assessment

| Issue | Title | Relevance to R13-H | Status on BASE_SHA |
|---|---|---|---|
| #1465 | bug(help,science_context): broker cache key drops typed failure cause | High (WP-10) | PARTIALLY_FIXED in `9052cb5d82` via `keyMat.failureDigest`. Need regression verification and edge-case testing. |
| #1466 | bug(teaching,suitability): suitability integer goal fail-open | High (WP-14) | FIXED in `src/suitability/suitability_goal.cpp` via `boundedIntegral`. Regression oracle required to lock down contract. |
| #1447 | bug(json): untyped jsoncpp access | Medium (WP-01/04/09) | Check all json parsing in planner, repair_planner, and science_context for untyped or throwing accesses. |
| #1450 | quality(authority): single-authority drift | Medium (WP-02/08/10) | SHA-256 and JSON canonicalizer consistency; avoid duplicate implementations. |

## Build and Environment Guardrails

- `CMAKE_BUILD_PARALLEL_LEVEL=2`
- `CTEST_PARALLEL_LEVEL=1`
- `QT_QPA_PLATFORM=offscreen`
- Global build lock: `flock -x /tmp/exp-rs-agent-locks/global-build.lock`
- Independent review subagent at final phase before PR creation.
