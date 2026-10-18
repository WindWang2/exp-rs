# R16-D-T08 Goal Loop Ledger (MapSpec/Cartography)

Oracle protocol: every WP = RED (real failing run, captured) → fix → GREEN ×2 (two consecutive identical-pass runs). No progress 3× on same hypothesis → re-scope; 8× → re-verify premises.

## Phase 0 — baseline
- [x] master @ 46791bc65, clean worktree, isolated branch hardening/r16-d-t08-cartography
- [x] 5 open PRs reviewed; T08 write-set intersection = ∅ (see ../r16-d/ISSUE_PR_BRANCH_MATRIX.md)
- [x] Read merged #1503/#1508/#1512 diffs for cartography/mapspec; audit explicitly excluded their fixed defects (12 rejected-suspicion list)
- [x] Audit findings T08-F01..F12 → WORK_PACKAGES.md (../r16-d/)

## Phase 2 — build & oracles
| Step | Status | Evidence |
|------|--------|----------|
| configure build-r16-d (Ninja, local Catch2) | done | build_t08.log (first attempt failed via dead proxy → FETCHCONTENT_SOURCE_DIR_CATCH2) |
| baseline binaries built from clean master; authoritative direct-binary baseline: test_mapspec 236/236 (RED run saw the 2 new-WP failures), cp11 21+5skip | | |
| RED per WP | ALL 11 RED captured (ctest #100/#103/#104/#126/#249/#298/#336-#350 + direct-binary detail) | |
| GREEN×2 per WP | | |

Note: implementer build/test commands queue behind the shared
/home/kevin/project/exp-rs-r16-build.lock (sibling worktrees build QGIS libs
through the same lease); flock -w 3600 per brief discipline.

## WP status
| WP | RED | Fix | GREEN×2 |
|----|-----|-----|---------|
| WP-T08-1 chart_registry asString (P1) | RED: test#104 FAILED 0.40s | chart_registry isString guards + validateChartSpec data/palette + mapspec colorbar typed check | GREEN×2: binary runs 2026-10-17 (test_mapspec 236/236, cp11 21+5skip host-gate, 0 fail ×2) |
| WP-T08-2 atlas page export (P1) | RED: test#336 FAILED | export.cpp atlas branch settings.pages={0} | GREEN×2: binary runs 2026-10-17 (test_mapspec 236/236, cp11 21+5skip host-gate, 0 fail ×2) |
| WP-T08-3 inset extent validation (P1) | RED: test#103 FAILED | mapspec.cpp "insets"->"inset_maps" | GREEN×2: binary runs 2026-10-17 (test_mapspec 236/236, cp11 21+5skip host-gate, 0 fail ×2) |
| WP-T08-4 vector scaledenominator ordering (P1) | RED: test#298 FAILED | style_spec.cpp vector ordering mirrors raster | GREEN×2: binary runs 2026-10-17 (test_mapspec 236/236, cp11 21+5skip host-gate, 0 fail ×2) |
| WP-T08-5 overlay_on remap (P2) | RED: test#347 FAILED | series_planner overlay_on idMap remap | GREEN×2: binary runs 2026-10-17 (test_mapspec 236/236, cp11 21+5skip host-gate, 0 fail ×2) |
| WP-T08-6 per-page geometry (P2) | RED: test#126 FAILED | composition mPageWidths/anchor + quality itemPageSize + whitespace off-by-one | GREEN×2: binary runs 2026-10-17 (test_mapspec 236/236, cp11 21+5skip host-gate, 0 fail ×2) |
| WP-T08-7 digest pages[] (P2) | RED: test#348 FAILED | quality structuralDigest folds pages[] | GREEN×2: binary runs 2026-10-17 (test_mapspec 236/236, cp11 21+5skip host-gate, 0 fail ×2) |
| WP-T08-8 point-layer series extent (P2) | RED: test#349 FAILED | series_planner degenerate bbox typed refusal | GREEN×2: binary runs 2026-10-17 (test_mapspec 236/236, cp11 21+5skip host-gate, 0 fail ×2) |
| WP-T08-9 tokenizer multi-dot (P2) | RED: test#100 FAILED | conditions stod full-consume check + verdict cleared on failure | GREEN×2: binary runs 2026-10-17 (test_mapspec 236/236, cp11 21+5skip host-gate, 0 fail ×2) |
| WP-T08-10 index label rect (P3) | RED: test#350 FAILED | series_planner index label rect from template page | GREEN×2: binary runs 2026-10-17 (test_mapspec 236/236, cp11 21+5skip host-gate, 0 fail ×2) |
| WP-T08-11 searchCores budget (P3) | RED: test#249 FAILED | composition searchCores per-target budget slice | GREEN×2: binary runs 2026-10-17 (test_mapspec 236/236, cp11 21+5skip host-gate, 0 fail ×2) |
