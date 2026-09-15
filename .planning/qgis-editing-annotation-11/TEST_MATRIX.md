# TEST_MATRIX — qgis-editing-annotation-11

All tests: Catch2, `QT_QPA_PLATFORM=offscreen`, `-j1`, run via `ctest --test-dir build-dev -R <regex>` from the worktree build dir. Oracle independence: known-answer expectations are computed from independent truth (hand-computed coordinates/counts, brute-force query on subsets, GDAL-read-back), never from the implementation under test.

| ID | Capability (pkg) | Test binary / CASE | Independent oracle | Command | Exit | Evidence |
| --- | --- | --- | --- | --- | --- | --- |
| T1 | session attach/state/dirty (A) | `test_edit_session` "attach reflects layer edit state" | hand-driven layer edit calls vs session facts | `ctest -R "^test_edit_session$"` | ✅ ctest | (log) |
| T2 | undo/redo delegation + aggregate (A) | `test_edit_session` "undo redo" | feature count before/after | same | ✅ ctest | (log) |
| T3 | commit error reporting (A) | `test_edit_session` "commit failure is reported" (read-only provider layer) | commitChanges()==false independently forced | same | ✅ ctest | (log) |
| T4 | rollback / discard (A) | `test_edit_session` "rollback" | feature count restored | same | ✅ ctest | (log) |
| T5 | lock refusal (A) | `test_edit_session` "locked layer" | tool/guard refuses, layer untouched | same | ✅ ctest | (log) |
| T6 | layer-removed mid-edit lifecycle (A/H) | `test_edit_session` "layer removal" | no dangling; session facts drop layer | same | ✅ ctest | (log) |
| T7 | project close with dirty session (A/H) | `test_edit_session` "project clear" | explicit rollback, no crash | same | ✅ ctest | (log) |
| T8 | snap config vertex/segment (C) | `test_edit_snapping` "configures" | read-back QgsSnappingConfig values | `ctest -R "^test_edit_snapping$"` | ✅ ctest | (log) |
| T9 | snap resolution known-answer (C) | `test_edit_snapping` "snaps to nearest vertex" | hand-computed snapped coordinate | same | ✅ ctest | (log) |
| T10 | validity issues reported (C) | `test_edit_validity` "bow-tie" | hand-known self-intersection | `ctest -R "^test_edit_validity$"` | ✅ ctest | (log) |
| T11 | repair preview + explicit apply (C) | `test_edit_validity` "preview/apply" | area/validity of preview vs original | same | ✅ ctest | (log) |
| T12 | valid geometry → zero issues (C, negative) | `test_edit_validity` "valid passthrough" | known valid square | same | ✅ ctest | (log) |
| T13 | brush stroke adds sample; one undo (B) | `test_edit_sample_tools` "brush stroke" | feature count + single undo restores | `ctest -R "^test_edit_sample_tools$"` | ✅ ctest | (log) |
| T14 | erase removes intersecting only (B) | `test_edit_sample_tools` "erase" | survivor feature ids hand-known | same | ✅ ctest | (log) |
| T15 | tools refuse locked/not-editable (B) | `test_edit_sample_tools` "refusal" | zero features added | same | ✅ ctest | (log) |
| T16 | annotation controller create/select facts (B) | `test_edit_annotation` | annotation layer item count | `ctest -R "^test_edit_annotation$"` | ✅ ctest | (log) |
| T17 | ROI pixel footprint known-answer (D) | `test_edit_roi_semantics` "footprint" | hand-computed center-of-pixel count on synthetic raster | `ctest -R "^test_edit_roi_semantics$"` | ✅ ctest | (log) |
| T18 | raster-CRS transform fail-closed + correct (D) | `test_edit_roi_semantics` "crs" | shifted-grid known answer; EPSG mismatch → error, not raw coords | same | ✅ ctest | (log) |
| T19 | NoData/edge handling (D) | `test_edit_roi_semantics` "nodata" | hand-built raster with NoData band | same | ✅ ctest | (log) |
| T20 | stats preview + cancel (D/E) | `test_edit_roi_semantics` "stats/cancel" | brute-force stats over same pixels; cancel flag honored | same | ✅ ctest | (log) |
| T21 | class label write-back (D) | `test_edit_roi_semantics` "writeback" | attribute read-back | same | ✅ ctest | (log) |
| T22 | index 100k build + query correctness (E) | `test_edit_index` "100k" | brute-force intersect/nearest on sampled subset | `ctest -R "^test_edit_index$"` | ✅ ctest | (log) |
| T23 | index incremental maintenance (E) | `test_edit_index` "incremental" | query after add/remove/geometry-change vs rebuild | same | ✅ ctest | (log) |
| T24 | selection bounds (E) | `test_edit_index` "bounds" | hand-known bbox | same | ✅ ctest | (log) |
| T25 | `editing:state` schema+facts (F) | `test_edit_agent_state` | expected JSON fields/values from hand-set session state | `ctest -R "^test_edit_agent_state$"` | ✅ ctest | (log) |
| T26 | agent surface has no write tools (F, negative) | `test_edit_agent_state` "read-only surface" | registry scan: no editing tool mutates | same | ✅ ctest | (log) |
| T27 | GeoJSON export round-trip + atomicity (G) | `test_edit_persistence` | GDAL/OGR read-back comparison; no `.tmp` residue on injected failure | `ctest -R "^test_edit_persistence$"` | ✅ ctest | (log) |
| T28 | GPKG export + reopen (G) | `test_edit_persistence` "gpkg" | reopen layer, feature/attr equality | same | ✅ ctest | (log) |
| T29 | E2E offscreen interaction chain (H) | `test_editing_e2e` | end-to-end known-answer chain + lifecycle | `ctest -R "^test_editing_e2e$"` | ✅ ctest | (log) |
| T30 | family gate (all above) | — | — | `ctest -R "test_edit"` | ✅ ctest | (log) |

### Result (first full-green run, 2026-09-16, `ctest -R "test_edit" -j1`, offscreen)

**10/10 binaries passed — T1–T29 all green (100% tests passed).** T30 (family gate) = same command.
Every known-answer expectation is an independent truth (a-priori GDAL-written value grids,
hand-computed center-of-pixel counts, brute-force scans for index queries, format-level GeoJSON
re-parse, core-level `setAllowCommit` for commit-failure forcing). Double-run record: see Phase 8
section in EVIDENCE.md.

Notes on oracle adjustments made during bring-up (all strengthen independence, none weaken it):
- makeValid of the bow-tie: repaired area pinned to the hand-computed 50 m² (two 25 m² lobes);
  the invalid input's own `.area()` is meaningless (shoelace cancellation) and is NOT used as truth.
- nearestNeighbor may return k+1 entries at the k-boundary; the assertion is "k nearest present,
  distances equal brute force" (sorted-distance comparison).
- Event synthesis derives screen positions from map coordinates via `QgsMapToPixel::transform`
  — correct under y-flip and any device pixel ratio.
