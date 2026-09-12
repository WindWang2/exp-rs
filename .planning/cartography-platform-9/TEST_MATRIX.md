# TEST MATRIX — what proves what

Primary target: `test_mapspec` (Catch2; contains test_mapspec.cpp,
test_cartography_{tokens,library,quality,templates,visual}.cpp,
test_platform{5,6,7,8}*.cpp, test_knowledge_drift.cpp and the new
test_platform9.cpp). Run: `ctest -R "^test_mapspec" -j1` in the worktree
build dir. Visual cases need the SICNU_CARTOGRAPHY_DATA_DIR define and
system QGIS — available on this host.

## Regression rules honored

- Bug fixes add tests the OLD code fails (verified by temporarily reverting
  where feasible, or by construction from the issue text).
- Scientific/geometry rules use known-answer assertions (exact mm/pt values),
  not "looks reasonable".
- Concurrency: none introduced by this track (registries stay
  mutex-guarded); no sleep-based tests.
- I/O: export tests assert final file state (atomic rename, digest match,
  no partial file on failure).
- UI lifecycle: compiler/layout tests run headless QApplication like the
  existing visual tests; object teardown asserted via QGIS layout
  deletion paths already in place.
- "not built / not run" is marked explicitly in FINAL_REPORT, never PASS.

## Matrix (9.0 additions)

| Area | Case | Kind | Old-fails-new-passes |
|---|---|---|---|
| M0 | #864 rollback known-answer | regression | yes (pre-8.0 fix code) |
| M0 | #865 zero-rect fit_content | regression | yes |
| M0 | #866 has() operand | regression | yes |
| M0 | #877 NaN comparators | regression | yes |
| M0 | ±Inf/mixed-kind ordering table | known-answer | semantics pin |
| M0 | ledger dedupe after core search | regression | yes (P2-1) |
| M1 | oscillation attribution | known-answer | yes |
| M1 | convergence trace bounds/shape | contract | yes |
| M1 | fit_content.text_ref sizing | known-answer | yes |
| M1 | scoped re-solve equivalence | property | yes |
| M2 | 3-page E2E + master furniture digest | known-answer digest | yes |
| M2 | page_break × keep_with | scenario | yes |
| M2 | invalid atlas expression → compile error | negative | yes |
| M2 | continuation page numbers | known-answer | yes |
| M3 | accuracy-matrix component applies | integration | new |
| M3 | accessibility fields present | drift guard | yes |
| M4 | multi-parent extends merge order | known-answer | yes |
| M4 | cyclic multi-parent rejected | negative | yes |
| M4 | diff_templates deltas | known-answer | new |
| M4 | catalog reference coverage | mechanical | extends existing |
| M5 | scale_ranges compile | integration | yes |
| M5 | bivariate contract gate | negative+positive | yes |
| M5 | change-map roundtrip | E2E | new |
| M6 | numeric formatting determinism | known-answer | yes |
| M6 | dual-axis gate | negative+positive | yes |
| M6 | furniture-over-map rule+repair | scenario | yes |
| M7 | kinsoku push-out known-answers | known-answer | yes |
| M7 | CJK golden render | visual golden | new |
| M8 | new rules fire/don't-fire | table | yes |
| M8 | repair ledger + convergence | scenario | yes |
| M8 | frame CRS validation | table | yes |
| M9 | atomic export success/failure | file-state | yes |
| M9 | page selection export | file-state | new |
| M9 | font diagnostics entry | contract | yes |
| M10 | explain bounded answer | contract | new |

## Explicitly not run on this host (recorded, not PASS)

- Windows/MSVC builds (existing master CI covers; not waited on).
- macOS renderer goldens.
- Anything requiring network data.
