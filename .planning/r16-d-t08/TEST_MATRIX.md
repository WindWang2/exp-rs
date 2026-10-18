# R16-D-T08 Test Matrix

Build dir: `build-r16-d` (private, Ninja Debug). Authoritative runs execute the two built binaries directly (ctest in this dir also registers ~960 `_NOT_BUILT` placeholders for test targets outside this track's build scope — see BASELINE correction note).

## Targets built & exercised
| Executable | Cases | Result (run 1 / run 2) |
|---|---|---|
| tests/test_mapspec | 236 (incl. 9 new R16-D cases/sections) | 236/236 pass ×2 (2805 assertions) |
| tests/test_cartography_production_11 | 26 | 21 pass + 5 skip ×2 (222 assertions, 0 fail) |

## New oracles (all RED on master first — evidence in GOAL_LOOP_LEDGER.md)
| WP | Test location | Assertion core |
|----|---------------|----------------|
| 1 | test_mapspec.cpp "Chart/colorbar string-typed fields are validated, not thrown" | numeric colorbar min/max → validation "must be a string" + compile refuses, NOTHROW; numeric chart label/palette → validateChartSpec problems, compile degrades |
| 2 | test_cartography_production_11.cpp "atlas export of a multi-page layout delivers one file per feature" | 2-page legend-free layout + 2 features → exactly 2 delivered files, dir set == reported set (RED: orphan `..._2.png` sibling outside the set) |
| 3 | test_mapspec.cpp CARTO-6 section + insetExtentProblems lambda | crossed/NaN/string inset extents → validateMapSpec problems; clean inset extent stays clean |
| 4 | test_platform9.cpp "M5: vector scaledenominator min/max ordering mirrors raster" | ascending order flagged "min must exceed max"; honest order clean; shape checks intact |
| 5 | test_cartography_production_11.cpp "series clones remap overlay_on..." | clone `overlay_on == "map-1-p1"`; preflight emits no MAP_CHART_OVER_MAP for the declared overlay |
| 6 | test_cartography_quality.cpp "Off-page and margin rules use the item's own page geometry" | page-1 item beyond the smaller sheet flagged (and clean placement clean); page-0 regression case intact |
| 7 | test_cartography_production_11.cpp "structural digest folds pages[] geometry" | pages[0] height/width changes and page-count changes all alter the digest |
| 8 | test_cartography_production_11.cpp "vector series over a point layer refuses degenerate extents" | planSeries returns null + problem naming degenerate bbox remediation; table series unaffected |
| 9 | test_mapspec.cpp "Multi-dot numbers are tokenizer problems..." | `a > 1.2.3` rejected with the token named; evaluate fails with error and cleared verdict; well-formed decimals regress clean |
| 10 | test_cartography_production_11.cpp "series index label derives its rect from the template page size" | portrait template → 0 preflight errors; rect within sheet, 20/30 margins |
| 11 | test_platform7.cpp "P7 solver: unsat-core budget is sliced per target" | after a wide unresolvable neighborhood, both disjoint witness pairs still get real cores (no "budget was exhausted", core size 2) |

## Pre-existing environment states (not caused by this track; A/B or provenance noted)
- 5 skipped cases in production_11: `skipIfHostRenderBroken()` gate — `QgsLayoutItemLegend::paint` hangs on this host in ANY legend-bearing render; differential evidence at `.planning/cartography-production-11/EVIDENCE.md` (pre-existing, identical on untouched master; opt-in via `SICNU_CP11_RENDER=1` reproduces the hang-class failures including the control atlas test).
- ctest-level strays observed in this dir: `pi_prompt_templates` / `pi_remote_sensing_assets` / `pi_scientific_workflow_compiler` (script tests over pi/**, unchanged by this track — fail by environment), `test_i18n_string_audit` (pre-existing, seen failing in the clean-master baseline run before any edit), `classroom_batch_conformance` (flaky/order-dependent: Failed in the first baseline full run, Passed on re-run; runner-script exit behavior, unrelated to cartography).
- Baseline correction: an earlier note recorded "1247 passed / 2 failed out of 1252" — that number was inferred from a 6-line tail and is RETRACTED. The honest ctest-level picture for this minimal build dir: ~960 `_NOT_BUILT` placeholders (targets outside this track's two binaries) + the direct-binary results above, which are the authoritative family verdict.
