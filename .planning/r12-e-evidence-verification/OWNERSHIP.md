# R12-E Ownership & Collision Protocol

## R12-E owned (write allowed)
src/verify/**, src/verification/**, src/verify_adapters/**, src/grader/**, src/faultlab/**,
src/preflight/**, src/suitability/**, src/explain/**, src/scientific_state/** (except coordination notes below),
tests/test_{grader,faultlab,fault,preflight,suitability,explain,scientific_state,verify}*.cpp (existing files — append test cases in-place),
.planning/r12-e-evidence-verification/** (sole writer: this worktree).

## Hard off-limits (other directions' active writes)
- src/analysis/**, src/core/**, src/operators/rs/**, src/processing/algorithms/** — R12-A (PR #1483 open)
- src/geospatial/**, src/data/**, src/dataset/**, src/experiment/** — R12-B
- src/agent/**, src/agent_ops/**, src/sdk/**, src/python/**, src/workflow/**, src/science_context/** (esp. asset_state_provider.*, PR #1484), src/runtime/chunk/** — R12-C
- src/app/**, src/gui/**, src/ui/**, src/help/**, src/teaching/**, src/teaching_admin/**, src/experiment_studio/** — R12-D
- src/operators/runtime/**, src/runtime/gpu/**, src/operators/io/**, src/operators/otb/** — R12-F
- cmake/**, scripts/**, packaging/**, tests/support/** — R12-G

## Shared single-writer files
| Path | Policy |
|---|---|
| tests/CMakeLists.txt | READ-ONLY by default. PRs #1483/#1484 already append here. R12-E policy: **add tests by appending cases to existing per-domain test files**. Recorded exceptions (2026-10-14): (1) F9 parity gate adds ONE link-library line (`sicnu_faultlab`) to the existing `test_verify_chain_locale_matrix` target (~:15265) — reason: E06 parity gate needs all four SHA-256 forks in one process; (2) `sicnu_faultlab` link added to existing `test_fault_matrix` target (:11021-11032) — reason: E12 benchmark must drive real faultlab scenarios; replaces a source-include hack with the proper link edge. Both are single-line additions to existing targets at anchors untouched by #1483/#1484; no new test target created. |
| src/preflight/CMakeLists.txt | Module-local build file, not in the locked set, untouched by #1483/#1484. Conditional exception for F5: ONLY IF routing the preflight digest through `sicnu::data::canonicalizeJsonRfc8785` requires a link edge and creates no dependency cycle. Evidence required in the F5 report; otherwise digest stays as-is (BLOCKED-WITH-EVIDENCE). |
| Root CMakeLists.txt, README, CHANGELOG, .goal-loop-ledger.md | untouched |

## Cross-domain observations handed off (not fixed by R12-E)
- #1450 radiometric vocabulary (src/core/radiometric_state.cpp vs rs_sar operators) → R12-A domain.
- #1450 workflow_checkpoint bounded reader → R12-C (claimed fixed by d5ab7bc0b0).
- #1447 non-faultlab/preflight sites (help, science_context, agent_ops, sdk, cli) → C/D.
- #1466 teaching/teaching_admin items → D.

## Build resource sharing
- Heavy builds: `flock "$HOME/.cache/exp-rs-heavy-build.lock"`, `-j2` max (`scripts/build.sh` enforces cap), ctest `-j1`, `QT_QPA_PLATFORM=offscreen`.
- R12-E build dir: `/home/kevin/projects/rs-studio/exp-rs-r12-e-verification/build-dev` (never points at main checkout; never cleans other worktrees' caches).
