# (title pending)

Generated: 2026-10-19T04:40:34Z

## Baseline

- base `origin/master` @ `a60c8831c9cc4e64701d17bde2ace67a74a9b7a0` (Merge pull request #1513 from WindWang2/hardening/r15-B-geodata-persistence)
- head `HEAD` @ `dccc17b8eec829753d6e9848f27599825cfca786` (docs(planning): review rounds R1/R2 records, performance notes, final test evidence)
- commits in range: 9

## Scope

- sources (9): `src/agent/cartography/export.cpp`, `src/agent/cartography/quality.cpp`, `src/agent/layout_tools/layout_service.cpp`, `src/agent/mapspec/mapspec.cpp`, `src/agent/mapspec/mapspec_compiler.cpp`, `src/app/main_window_connections.cpp`, `src/app/main_window_menus.cpp`, `src/app/main_window_vector.cpp`, `src/app/workbench/selection_context.cpp`
- public_headers (2): `src/app/main_window.h`, `src/app/workbench/selection_context.h`
- tests (5): `tests/test_cartography_production_11.cpp`, `tests/test_cartography_quality.cpp`, `tests/test_layout_tools.cpp`, `tests/test_mapspec.cpp`, `tests/test_r14_D_vector_edit_honesty.cpp`
- docs (11): `.planning/r18-d-desktop/BASELINE.md`, `.planning/r18-d-desktop/CROSS_TRACK_FINDINGS.md`, `.planning/r18-d-desktop/DECISIONS.md`, `.planning/r18-d-desktop/LOOP_LEDGER.md`, `.planning/r18-d-desktop/ORACLES.md`, `.planning/r18-d-desktop/OWNERSHIP.md`, `.planning/r18-d-desktop/PERFORMANCE.md`, `.planning/r18-d-desktop/REVIEW_R1.md`, `.planning/r18-d-desktop/REVIEW_R2.md`, `.planning/r18-d-desktop/TASKS.md`, `.planning/r18-d-desktop/TEST_RESULTS.md`
- other (1): `.gitignore`

## Non-goals

<!-- TODO: what this PR deliberately does not do -->

## Design

<!-- TODO: design notes and alternatives rejected -->

## Issue mapping

<!-- TODO: issue numbers this PR closes or relates to -->

## Tests

<!-- TODO: exact commands and their output, run twice -->

## Resources

<!-- TODO: build parallelism used (-j1/-j2), targeted vs full runs -->

## Review disposition

<!-- TODO: reviewer findings and their fix state -->

## Known limitations

<!-- TODO: deferred P2/P3 with reproducible evidence, or 'none' -->

## Conflict hotspots

overlap evidence for this diff's 28 paths:
- open_prs: 2
  - `agent/r18-c-pi-mcp-workflow` touches 1 of these paths
  - `agent/r18-b-model-runtime-perf` touches 1 of these paths
- merged_prs: 8
  - `hardening/r16-d-workbench-cartography` touches 11 of these paths
  - `hardening/r16-c-teaching-classroom` touches 1 of these paths
  - `hardening/r16-b-scientific-decisions` touches 1 of these paths
  - `hardening/r15-E-desktop-teaching-help` touches 1 of these paths
  - `hardening/r15-D-pi-mcp-workflow` touches 1 of these paths
  - `hardening/r15-C-model-runtime` touches 1 of these paths
  - `hardening/r15-A-science-processing` touches 1 of these paths
  - `hardening/r14-D-qt-qgis-cartography` touches 10 of these paths
- remote_branches: 2
  - `origin/agent/r18-b-model-runtime-perf` touches 1 of these paths
  - `origin/agent/r18-c-pi-mcp-workflow` touches 1 of these paths
- sibling_worktrees: 0

---

## Diff stat

```
.gitignore                                      |   4 +
 .planning/r18-d-desktop/BASELINE.md             |  21 ++
 .planning/r18-d-desktop/CROSS_TRACK_FINDINGS.md |  15 ++
 .planning/r18-d-desktop/DECISIONS.md            |  14 ++
 .planning/r18-d-desktop/LOOP_LEDGER.md          |  16 ++
 .planning/r18-d-desktop/ORACLES.md              |  16 ++
 .planning/r18-d-desktop/OWNERSHIP.md            |  27 +++
 .planning/r18-d-desktop/PERFORMANCE.md          |  23 ++
 .planning/r18-d-desktop/REVIEW_R1.md            |  18 ++
 .planning/r18-d-desktop/REVIEW_R2.md            |  37 +++
 .planning/r18-d-desktop/TASKS.md                |  29 +++
 .planning/r18-d-desktop/TEST_RESULTS.md         |  61 +++++
 src/agent/cartography/export.cpp                |   9 +-
 src/agent/cartography/quality.cpp               |  65 +++--
 src/agent/layout_tools/layout_service.cpp       |   5 +-
 src/agent/mapspec/mapspec.cpp                   |  22 ++
 src/agent/mapspec/mapspec_compiler.cpp          |  29 ++-
 src/app/main_window.h                           |  11 +
 src/app/main_window_connections.cpp             |  47 +++-
 src/app/main_window_menus.cpp                   | 128 +++++-----
 src/app/main_window_vector.cpp                  |  46 +++-
 src/app/workbench/selection_context.cpp         |  20 ++
 src/app/workbench/selection_context.h           |   8 +-
 tests/test_cartography_production_11.cpp        | 101 ++++++++
 tests/test_cartography_quality.cpp              |  84 +++++++
 tests/test_layout_tools.cpp                     |  23 ++
 tests/test_mapspec.cpp                          |  83 +++++++
 tests/test_r14_D_vector_edit_honesty.cpp        | 307 ++++++++++++++++++++++++
 28 files changed, 1184 insertions(+), 85 deletions(-)
```

## Commits

```
dccc17b8e docs(planning): review rounds R1/R2 records, performance notes, final test evidence
c889bbb4f fix(app): review R18-D R2 P3 follow-ups — menu-twin test surface, stale comment, deliberate-strict note
de0db471d fix(app,cartography): review R18-D R1 — trim/extend polygon mask, full menu gate, field-level atlas refusal (R18-D R1)
5ed7a831a docs(planning): R18-D baseline, ownership, oracles, ledger, test evidence
ca063926e fix(cartography,mapspec): atlas filename_expression compile check + NaN-safe export dpi guard (R18-D)
9bccdc177 fix(cartography): single-page png selection delivers the selected page atomically (R18-D)
2bea3eece fix(cartography): rect repairs clamp against the item's own sheet (R18-D)
97b4e597e fix(mapspec): typed refusal for numeric text furniture and legend titles (R18-D)
99cd5b02e fix(app): honest editing interaction state across shell, menu, and projection (R18-D)
```
