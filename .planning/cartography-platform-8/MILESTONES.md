# MILESTONES — cartography-platform-8 (execution log)

## M0 Baseline — DONE
- Worktree created from origin/master @ `2d4f0daedd`; master untouched.
- Baseline evidence on the Linux host: master build dir `test_mapspec`
  target green; cartography ctest selection 52/52 PASS (60 s).
- Worktree configured (Ninja/Release, system QGIS+Qt6) and `test_mapspec`
  green; PNG `[visual]` determinism/golden cases verified PASSING locally
  (the 7.0 Windows-headless limitation does not apply here).

## M1 NoData wiring — DONE (commit 1183990f5c)
`applyRasterNodata` in style_compiler; optional `color` validated; report
strings; buildRasterRenderer mirrors shading. Tests 1–3.

## M2 Locator connectors — DONE (commits 88c3bf6555 + tests)
LayoutService `line`/`polyline` item type (QgsLayoutItemPolyline; park at
origin + bounding-box placement + local nodes — QGIS `setNodes` maps
through the item position, so raw scene nodes are contaminated; the local
protocol is exact, verified numerically). Compiler connector block with
[xmin,ymin,xmax,ymax] extent math (an initial precedence bug — center
computed as xmin+xmax/2 — was caught by the exact-geometry test and fixed).
Validation for style/stroke/color. Tests 4–6.

## M3 MapSpec v5 — DONE (commit 88c3bf6555)
Output declaration validation; typed binding validation (shape-checked,
not key-closed); upgrade re-stamps to 5; migration doc. Tests 7–8.

## M4 Page-aware solver — DONE (commit 05117ea683)
Page-height map in normalize; keep_with/avoid_overlap overflow → permanent
Failed with `page_overflow` evidence; failReason preserved into violation
reasons. Tests 9–10.

## M5 Typography 2.0 — DONE (commit 05117ea683)
`isTextBreakPolicy` + halfwidth line-end compression through the wrap/fit
engine; `line_height` plumb-through in the preflight wrap rule; declared
font fields validated. Tests 11–12.

## M6 NoData legend QA — DONE (commits 1183990f5c)
MAP_NODATA_LEGEND rule + converging repair + catalog entry; compiler
swatch composite. Tests 13–14.

## M7 Compose identity + chart labels — DONE (commit 1183990f5c)
compose returns structural_digest/provenance/declared_output;
confirmMapOutput surfaces them; bar-family labels elide. Test 15.

## M8 Evidence + docs — DONE (commit 962625ee28/f0be87eaaf)
Linux PNG golden evidence recorded (visual-regression.md); limitations
corrected (connector drawn; nodata applied; 5.0-era stale text entries
repaired); mapspec-reference current-version header fixed (said 3.0 while
code was at v4) + Platform 8.0 notes; migration-mapspec-v5.md; preflight
catalog doc; CHANGELOG entry.

## M9 Adversarial review — see REVIEW_LOG.md

## M10 Integration — FINAL_REPORT.md, push, PR
