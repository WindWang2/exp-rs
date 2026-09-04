# Project: PR Integration, Build Verification & Repository Cleanup

## Architecture
- Repository: `exp-rs` (C++20 / Qt 6.8+ / GDAL / PROJ / GEOS / OpenCV 5 / Catch2 v3.7.1)
- Main Branch: `master`
- Integration Workflow: Sequential squash-and-merge of 5 open PRs (#708 -> #709 -> #710 -> #711 -> #712)
- Build System: CMake (Release + Ninja in `build/`)
- Test Runner: CTest with Catch2 (`QT_QPA_PLATFORM=offscreen LD_LIBRARY_PATH=/usr/lib ctest --test-dir build --output-on-failure -j$(nproc)`). CTestCustom.cmake also pins `PYTHONHOME`/`PYTHONPATH` and `QT_IM_MODULE=compose` (see TEST_INFRA.md). `LD_PRELOAD=/usr/lib/libxml2.so` is not the documented policy; if used, `PYTHONHOME` must match the CMake-discovered interpreter.
- Secondary Worktrees: Cleaned up (0 secondary worktrees remaining)

## Feature Inventory
| # | Feature | Description | Milestone | Source |
|---|---------|-------------|-----------|--------|
| 1 | PR #708 Integration | Merge `perf/operator-ization` (20 commits, determinism grades, cache hits, TaskCenter GC). Resolve 5 conflicts with master by accepting HEAD. | M1 | ORIGINAL_REQUEST §R1 |
| 2 | PR #709 Integration | Merge `zcode/resolve-all-open-issues-20260830` (33 commits, 39 bugfixes). Resolve conflicts by accepting HEAD. | M1 | ORIGINAL_REQUEST §R1 |
| 3 | PR #710 Integration | Merge `feat/cartography-layout-studio` (10 commits, layout studio). Clean merge. | M1 | ORIGINAL_REQUEST §R1 |
| 4 | PR #711 Integration | Merge `feat/spatial-execution-platform` (48 commits, spatial platform 1.0 convergence). | M1 | ORIGINAL_REQUEST §R1 |
| 5 | PR #712 Integration | Merge `feat/temporal-rs-analysis` (7 commits, multi-temporal RS engine). Combine temporal resolver and numeric scale in rs_spectral_index_operator. | M1 | ORIGINAL_REQUEST §R1 |
| 6 | CMake Build Verification | Build master with `cmake --build build` (zero compilation/linking errors). | M2 | ORIGINAL_REQUEST §R2 |
| 7 | Catch2 Test Verification | Historical "2126 tests / 2123 passed / 3 skipped / 0 failed" figure is stale (suite size moved; #730 showed 16/2147 red on the old `LD_PRELOAD` command, 15 of them environment). Documented runner is now `QT_QPA_PLATFORM=offscreen LD_LIBRARY_PATH=/usr/lib ctest --test-dir build --output-on-failure -j$(nproc)` with CTestCustom env pins. Do not treat "100% green" as a current claim without a fresh `ctest` log. | M2 | ORIGINAL_REQUEST §R2 |
| 8 | Secondary Worktree Cleanup | Kill background processes in worktree and remove 4 secondary worktrees with `git worktree remove --force`. | M3 | ORIGINAL_REQUEST §R3 |
| 9 | Branch Cleanup | Prune and remove local and remote feature branches (`git branch -D`, `git push origin --delete`, `git remote prune origin`). | M3 | ORIGINAL_REQUEST §R3 |
| 10 | Final Audit & Verification | Forensic audit, review, and verification against all acceptance criteria. | M4 | ORIGINAL_REQUEST §Acceptance Criteria |

## Milestones
| # | Name | Scope | Dependencies | Status |
|---|------|-------|-------------|--------|
| M0 | Repository Survey | Inspect PR details, branch status, worktree paths, and CMake test configuration | None | DONE |
| M1 | Sequential PR Squash-Merge | Merge PRs #708, #709, #710, #711, #712 into master with conflict resolution | M0 | DONE |
| M2 | Build & Catch2 Test Verification | Full CMake build and Catch2 test run on master | M1 | DONE |
| M3 | Worktree & Branch Cleanup | Remove 4 secondary worktrees and delete local/remote branches | M1 | DONE |
| M4 | Final Integrity Audit & Verification | Forensic audit and final verification against acceptance criteria | M2, M3 | IN_PROGRESS |

## Interface Contracts
- Git branch integration: All feature branches squash-merged into `master`.
- GitHub PR status: All 5 PRs merged and closed (`gh pr list` shows 0 open PRs).
- Build target: `cmake --build build` succeeds with 0 errors.
- Test runner: `QT_QPA_PLATFORM=offscreen LD_LIBRARY_PATH=/usr/lib ctest --test-dir build --output-on-failure -j$(nproc)`. CTestCustom.cmake pins `PYTHONHOME`, prepends `/usr/lib`, and sets `QT_IM_MODULE=compose`. The old `LD_PRELOAD=/usr/lib/libxml2.so` + "100% green / 0 failed" line was stale as of #730.
- Worktree state: `git worktree list` outputs only `/home/kevin/projects/rs-studio/main`.
- Branch state: No secondary local or remote feature branches remain.

## Code Layout
- Repository root: `/home/kevin/projects/rs-studio/main`
- Build directory: `build`
- Core libraries: `src/core`, `src/gui`, `src/data`, `src/processing`, `src/operators`, `src/workflow`, `src/jobs`, `src/agent`
- Applications: `src/app` (`sicnu_geo_rs`), `src/cli` (`sicnu_geo_rs_cli`)
- Tests: `tests/` (Catch2 test executables)
- Agent metadata: `.agents/`
