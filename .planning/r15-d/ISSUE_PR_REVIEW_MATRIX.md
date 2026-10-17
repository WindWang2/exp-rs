# R15-D Issue / PR / review matrix

Read live (gh + git) on 2026-10-17 against master 46791bc65. Classifications:
FIXED_ON_MASTER / COVERED_BY_OPEN_PR / REPRODUCED_OPEN / NOT_REPRODUCIBLE /
OUT_OF_SCOPE.

## PRs

| PR | State at start | Overlap with D | Action |
|---|---|---|---|
| #1505 (R13-J security/concurrency) | MERGED | locked src/python/isolated + science_context | read-only, treated as fixed ground truth |
| #1509 (R13-K JsonCpp type safety) | MERGED (in 46791bc65) | locked 4 agent_ops cpp | merged before branch point; our F7 is a NEW site it missed (task_center_executor.cpp) |
| #1512 (R14-D Qt/QGIS/cartography) | MERGED | locked cartography/mapspec/layout_tools | untouched |
| #1507 (R14-C agent/workflow) | MERGED | prior D-area hardening (FIX-1..9) | regression-verified, not re-fixed |
| #1513 (R15-B geodata) | OPEN, 0 reviews | data/dataset/experiment/geospatial/cli | disjoint file set |
| #1515 (R15-C model runtime) | OPEN (seen at final refresh) | src/operators/, src/runtime/chunk/, tests/CMakeLists.txt, .gitignore | disjoint; only shared .gitignore tail (trivial) |
| #1514 (R15-A science-processing) | OPEN, 0 reviews | analysis/operators/processing image fusion | disjoint file set |

## Issues (verdict against master, not issue state)

| Issue | Classification | Note |
|---|---|---|
| #1462 plugin/worker lifecycle+IPC | partly FIXED_ON_MASTER; residual worker write-gate defect REPRODUCED -> F6 | worker pool files were #1505's, untouched |
| #1463 journal/bench/recipes | FIXED_ON_MASTER | audit only |
| #1460 external process/CLI | FIXED_ON_MASTER | audit only |
| #1459 workflow/runtime | FIXED_ON_MASTER | audit only |
| #1448 checkpoint/terminal | FIXED_ON_MASTER (D17 lane fixed in #1507) | our F3 adds parser-level fail-closed |
| #1447 JsonCpp type safety | FIXED_ON_MASTER; F5/F7 are NEW sites it missed | |
| #1499 stale compile-break report | stale | |
| #1456 geospatial SSRF/Windows paths | F4 reproduces the Windows-junction half | POSIX symlink half covered by existing tests |
| #1454/#1455/#1449 processing/TaskCenter races | no new reproduction on master | executor F7 is a crash, not a race |
| #1488/1489/1490/1491/1494/1495/1458 | OUT_OF_SCOPE for D | ci/i18n/int64/repro-bundle lanes |

## Newly reproduced defects fixed by this branch

F1 Pi failed-start liveness; F2 MCP EOF sentinel; F3 workflow gates/inputs
fail-closed + checkpoint exception conversion; F4 Windows junction escape;
F5 snapshot leaf coercion; F6 worker write-gate fail-open; F7 executor
isMember throw; F8 checkpoint temp-name family (dead sweep / wrong-run
lock probe / rng>LLONG_MAX / staged temps listed as checkpoints /
committed ".tmp"-runId hidden); F9 4 MiB boundary line + badbit spin;
F10 gates.require/hint strictness. See ORACLES.md.
