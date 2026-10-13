# Baseline — Direction D: Qt/QGIS Workbench, Interaction States, Teaching & Help

## Remote & Worktree Baseline

- **Timestamp**: 2026-10-13 18:19:00 +0800
- **Repository**: `https://github.com/WindWang2/exp-rs`
- **Track**: `r11-d-qt-teaching-ux`
- **Worktree**: `/home/kevin/projects/rs-studio/exp-rs-r11-d-qt-ux`
- **Branch**: `hardening/r11-d-qt-teaching-ux`
- **Base SHA**: `8a08642760e2e8ffd2c4b965fc4683c01d3e4758` (Merge hardening/r6-digest-identity-convergence: MSVC configure unblock)

## Remote State

```text
git remote -v:
origin	https://github.com/WindWang2/exp-rs.git (fetch)
origin	https://github.com/WindWang2/exp-rs.git (push)

origin/master HEAD: 8a08642760e2e8ffd2c4b965fc4683c01d3e4758
```

Recent commits on `origin/master`:
1. `8a08642760` Merge hardening/r6-digest-identity-convergence: MSVC configure unblock (WHOLE_ARCHIVE twin-item conflict)
2. `09a7ef5bd6` Merge rs14-unified-verifier: unified scientific verifier subsystem (RS14, ADR branch doc)
3. `84d782c408` Merge pull request #1474 from WindWang2/hardening/r8-ui-teaching-context
4. `78148cc006` Merge origin/master (PRs #1469-#1473) into hardening/r8-ui-teaching-context
5. `cec4dc4491` Merge pull request #1473 from WindWang2/hardening/r8-data-experiment-integrity
6. `f0e44192a6` Merge pull request #1472 from WindWang2/hardening/r8-science-algorithm-closure

## Open PRs Inventory

| PR # | Head Branch | Scope | Risk / Overlap Status |
|---|---|---|---|
| **#1478** | `origin/hardening/r10-ui-teaching-help-consistency` | Qt/QGIS workbench, teaching admin batch discovery, workflow session controller, georef dual, help topics | **IN_OPEN_PR**: Examined exact file diffs; avoid directly colliding on unfinished lines, build upon resolved interfaces or deferred residual backlog items. |
| **#1477** | `origin/hardening/r9-agent-execution-reliability` | IPC replay, program resolution, session coalescing, model_runtime, task_center | **OUT_OF_SCOPE**: Agent and execution runtime files only. Zero overlap. |
| **#1476** | `origin/hardening/r9-ui-teaching-test-quality` | `guided_workflow_widget`, `rs_scan_pool.h`, `script_adapters`, `fault_transforms` | **IN_OPEN_PR**: Do not duplicate #1476 fixes. Keep quarantined. |
| **#1475** | `origin/hardening/r9-science-data-integrity` | Science algorithms, radiometric state, query planner, dataset store | **OUT_OF_SCOPE**: Pure science and data layer. Zero overlap. |

## Sibling R11 Tracks

- `r11-a-science-algorithms` (`/home/kevin/projects/rs-studio/exp-rs-r11-a-science`)
- `r11-b-geospatial-data` (`/home/kevin/projects/rs-studio/exp-rs-r11-b-geospatial`)
- `r11-c-agent-execution` (`/home/kevin/projects/rs-studio/exp-rs-r11-c-agent`)
- `r11-d-qt-teaching-ux` (`/home/kevin/projects/rs-studio/exp-rs-r11-d-qt-ux` — this worktree)

## Platform & Toolchain

- OS: Linux (kernel x86_64)
- Compiler: GCC 16 / Clang++ 20, C++20 standard
- CMake: 4.4.4
- Build System: Ninja 1.13.2
- Parallel build constraint: Strict `-j2` (`CMAKE_BUILD_PARALLEL_LEVEL=2`, `cmake --build --parallel 2`)
- Test execution constraint: `ctest -j1`, `CTEST_PARALLEL_LEVEL=1`, `QT_QPA_PLATFORM=offscreen`
