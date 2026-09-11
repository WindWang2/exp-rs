# TEST MATRIX — cartography-platform-8

All tests deterministic and resource-bounded (fixtures ≤ 64 raster cells,
≤ 300-entry arrays, bounded solver/typography budgets). Runner:
`ctest -R "P8" -j1` in the worktree build; suite tags `[platform8]`.

| # | Test | Milestone | Kind | Status |
| --- | --- | --- | --- | --- |
| 1 | nodata value reaches the provider as a user nodata range (real GeoTIFF, idempotent re-apply, transparent default shading) | M1 | integration (QGIS) | PASS |
| 2 | non-transparent nodata shades pixels through the renderer (pseudocolor + white shading) | M1 | integration (QGIS) | PASS |
| 3 | nodata default shading is black; validation rejects non-string color; minimal declaration stays valid | M1 | known-answer | PASS |
| 4 | locator connector compiles to a QGIS polyline (projected extent anchor; exact page geometry via item position + local nodes) | M2 | known-answer + integration | PASS |
| 5 | connector without resolvable extents anchors at the frame center | M2 | known-answer | PASS |
| 6 | locator connector surface is validated (style/stroke/color) | M2 | validation | PASS |
| 7 | v5 output declarations validate; malformed dpi/format/dir each produce one targeted problem; upgrade stamps v5 | M3 | validation + migrate | PASS |
| 8 | binding surface shape validation (mode/layer/params/data types, square matrix, 256-entry budget) | M3 | validation | PASS |
| 9 | page-aware pins refused with page_overflow evidence; geometry untouched; taller-page control applies the same pin | M4 | known-answer | PASS |
| 10 | avoid_overlap refuses a page-crossing push-down | M4 | known-answer | PASS |
| 11 | halfwidth break policy compresses line-final punctuation (5.5 em known answer; unknown policy ≡ none; report carries the resolved policy) | M5 | known-answer | PASS |
| 12 | declared font typography surface validates (break_policy, line_height) | M5 | validation | PASS |
| 13 | nodata legend rule fires and repair converges (registered style + legend; re-preflight clean) | M6 | contract (repair loop) | PASS |
| 14 | legend nodata renders as a swatch composite (shape + label materialize) | M6 | integration (QGIS) | PASS |
| 15 | compose surfaces structural_digest (== structuralDigest of resolved spec, 64 hex), provenance (template + components), declared output | M7 | contract | PASS |

## Regression evidence (same worktree, Linux, QT_QPA_PLATFORM=offscreen)

- Master baseline `test_mapspec` cartography suite: **52/52 PASS**
  (`main/build`, pre-change code, 60 s) — establishes the Linux toolchain.
- Post-change cartography sweep (MapSpec/Cartography/P5/P6/P7/visual/
  golden/symbology/knowledge/drift): **59/62 PASS**; the 3 non-passes are
  unrelated `_NOT_BUILT` harness/IO targets not compiled in this worktree
  (they were matched only by the `golden` name filter).
- `[visual][determinism]` PNG hashing and `[visual][golden]` reference
  comparison: **PASS with real QgsLayoutExporter renders** (~10 golden
  scenes written to SICNU_CARTOGRAPHY_GOLDEN_DIR and compared).
- Harness plan/agent tool targets (plan_tools.cpp touched): built and run —
  see FINAL_REPORT for the recorded counts.
- Intentional pin update: `test_platform7.cpp` version constant 4 → 5
  (documented in the test comment and the migration doc).
