# TEST_MATRIX — qgis-editing-annotation-11

All tests: Catch2, `QT_QPA_PLATFORM=offscreen`, `-j1`, run via `ctest --test-dir build-dev -R <regex>` from the worktree build dir. Oracle independence: known-answer expectations are computed from independent truth (hand-computed coordinates/counts, brute-force query on subsets, GDAL-read-back), never from the implementation under test.

| ID | Capability (pkg) | Test binary / CASE | Independent oracle | Command | Exit | Evidence |
| --- | --- | --- | --- | --- | --- | --- |
| T1 | session attach/state/dirty (A) | `test_edit_session` "attach reflects layer edit state" | hand-driven layer edit calls vs session facts | `ctest -R "^test_edit_session$"` | ☐ | ☐ |
| T2 | undo/redo delegation + aggregate (A) | `test_edit_session` "undo redo" | feature count before/after | same | ☐ | ☐ |
| T3 | commit error reporting (A) | `test_edit_session` "commit failure is reported" (read-only provider layer) | commitChanges()==false independently forced | same | ☐ | ☐ |
| T4 | rollback / discard (A) | `test_edit_session` "rollback" | feature count restored | same | ☐ | ☐ |
| T5 | lock refusal (A) | `test_edit_session` "locked layer" | tool/guard refuses, layer untouched | same | ☐ | ☐ |
| T6 | layer-removed mid-edit lifecycle (A/H) | `test_edit_session` "layer removal" | no dangling; session facts drop layer | same | ☐ | ☐ |
| T7 | project close with dirty session (A/H) | `test_edit_session` "project clear" | explicit rollback, no crash | same | ☐ | ☐ |
| T8 | snap config vertex/segment (C) | `test_edit_snapping` "configures" | read-back QgsSnappingConfig values | `ctest -R "^test_edit_snapping$"` | ☐ | ☐ |
| T9 | snap resolution known-answer (C) | `test_edit_snapping` "snaps to nearest vertex" | hand-computed snapped coordinate | same | ☐ | ☐ |
| T10 | validity issues reported (C) | `test_edit_validity` "bow-tie" | hand-known self-intersection | `ctest -R "^test_edit_validity$"` | ☐ | ☐ |
| T11 | repair preview + explicit apply (C) | `test_edit_validity` "preview/apply" | area/validity of preview vs original | same | ☐ | ☐ |
| T12 | valid geometry → zero issues (C, negative) | `test_edit_validity` "valid passthrough" | known valid square | same | ☐ | ☐ |
| T13 | brush stroke adds sample; one undo (B) | `test_edit_sample_tools` "brush stroke" | feature count + single undo restores | `ctest -R "^test_edit_sample_tools$"` | ☐ | ☐ |
| T14 | erase removes intersecting only (B) | `test_edit_sample_tools` "erase" | survivor feature ids hand-known | same | ☐ | ☐ |
| T15 | tools refuse locked/not-editable (B) | `test_edit_sample_tools` "refusal" | zero features added | same | ☐ | ☐ |
| T16 | annotation controller create/select facts (B) | `test_edit_annotation` | annotation layer item count | `ctest -R "^test_edit_annotation$"` | ☐ | ☐ |
| T17 | ROI pixel footprint known-answer (D) | `test_edit_roi_semantics` "footprint" | hand-computed center-of-pixel count on synthetic raster | `ctest -R "^test_edit_roi_semantics$"` | ☐ | ☐ |
| T18 | raster-CRS transform fail-closed + correct (D) | `test_edit_roi_semantics` "crs" | shifted-grid known answer; EPSG mismatch → error, not raw coords | same | ☐ | ☐ |
| T19 | NoData/edge handling (D) | `test_edit_roi_semantics` "nodata" | hand-built raster with NoData band | same | ☐ | ☐ |
| T20 | stats preview + cancel (D/E) | `test_edit_roi_semantics` "stats/cancel" | brute-force stats over same pixels; cancel flag honored | same | ☐ | ☐ |
| T21 | class label write-back (D) | `test_edit_roi_semantics` "writeback" | attribute read-back | same | ☐ | ☐ |
| T22 | index 100k build + query correctness (E) | `test_edit_index` "100k" | brute-force intersect/nearest on sampled subset | `ctest -R "^test_edit_index$"` | ☐ | ☐ |
| T23 | index incremental maintenance (E) | `test_edit_index` "incremental" | query after add/remove/geometry-change vs rebuild | same | ☐ | ☐ |
| T24 | selection bounds (E) | `test_edit_index` "bounds" | hand-known bbox | same | ☐ | ☐ |
| T25 | `editing:state` schema+facts (F) | `test_edit_agent_state` | expected JSON fields/values from hand-set session state | `ctest -R "^test_edit_agent_state$"` | ☐ | ☐ |
| T26 | agent surface has no write tools (F, negative) | `test_edit_agent_state` "read-only surface" | registry scan: no editing tool mutates | same | ☐ | ☐ |
| T27 | GeoJSON export round-trip + atomicity (G) | `test_edit_persistence` | GDAL/OGR read-back comparison; no `.tmp` residue on injected failure | `ctest -R "^test_edit_persistence$"` | ☐ | ☐ |
| T28 | GPKG export + reopen (G) | `test_edit_persistence` "gpkg" | reopen layer, feature/attr equality | same | ☐ | ☐ |
| T29 | E2E offscreen interaction chain (H) | `test_editing_e2e` | end-to-end known-answer chain + lifecycle | `ctest -R "^test_editing_e2e$"` | ☐ | ☐ |
| T30 | family gate (all above) | — | — | `ctest -R "test_edit"` | ☐ | ☐ |

Evidence column filled with command exit codes + run dates during Phases 1–6; final double-run recorded in Phase 8.
