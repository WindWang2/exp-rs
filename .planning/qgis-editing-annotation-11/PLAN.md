# PLAN — qgis-editing-annotation-11 execution order

Baseline: `origin/master` @ `a5b11b7f10`. Master is READ-ONLY. All work in this worktree.

All new business code lives in **`src/app/editing/`** (new app-level editing foundation layer). Vendored QGIS (`src/gui`, `src/core`, `src/analysis` core files) stays **unmodified** — new tools subclass existing gui classes. Shared integration files (`src/app/CMakeLists.txt`, `tests/CMakeLists.txt`, `.gitignore`, `CHANGELOG.md`) get append-only minimal diffs only.

## Phase 0 — baseline + planning (done at write time)
- Audit origin/PRs/issues (BASELINE.md), ownership map (PARALLEL_OWNERSHIP.md), architecture map (CURRENT_ARCHITECTURE.md), GOAL verbatim, ledger seed.
- `.gitignore` whitelist for `.planning/qgis-editing-annotation-11/` (D18/D19 whole-dir style), verified with `git check-ignore -v`.

## Phase 1 — Package A: `RsEditSession` authority
- `src/app/editing/rs_edit_session.{h,cpp}`:
  - Attaches/detaches `QgsVectorLayer`s to a session; per-layer `RsLayerEditState {editMode, modified, undoDepth, redoDepth, featureCount, selectedCount, locked}`.
  - Aggregated facts: `isDirty()`, `editableLayers()`, per-layer undo/redo delegation (wraps `QgsVectorLayer::undoStack()`), `beginEditCommand/endEditCommand` RAII guard (`RsEditCommandGuard`).
  - Commit/rollback with collected error strings (never silently ignore `commitChanges()` return).
  - Lifecycle safety: `QPointer` layers + `QgsProject::instance()->layerRemoved` / `willRemoveLayer` handling → state drops cleanly, no dangling; `projectClose` → explicit rollback-or-block decision recorded in state, no crash.
  - Signals `stateChanged()`, `layerStateChanged(QString layerId)`.
- Tests `tests/test_edit_session.cpp`: known-answer (begin/modify/undo/redo/commit), negative (commit failure reporting on a read-only-data-source layer), lifecycle (delete layer mid-edit, project clear), lock (locked layer refuses edits).

## Phase 2 — Packages B+C: tools + snapping/validity
- `src/app/editing/rs_snapping_controller.{h,cpp}`: vertex/segment/endpoint snap config over `QgsMapCanvas::snappingUtils()` (project-level `QgsSnappingConfig`), enable/disable, layer filter, tolerance in layer/map units; read-back getters for tests/agent.
- `src/app/editing/rs_geometry_validity.{h,cpp}`: `validate(geometry) → issues[]` using `QgsGeometry::validateGeometry` (Qgis::GeometryValidationMethod), `repairPreview(geometry) → QgsGeometry` via `makeValid` **without** applying; explicit `applyRepair` is a separate opt-in call. Never silently mutates.
- `src/app/editing/rs_sample_brush_tool.{h,cpp}` + `rs_sample_erase_tool.{h,cpp}`: raster-sample painting on a memory polygon sample layer — brush = disc stamp polygon added per stroke (merged per drag into one edit command), erase = removes/de-clips features intersecting the stamp. Emit `strokeCommitted(int added/removed)`; work off `RsEditSession`-attached layer; refuse when layer locked/not editable.
- `src/app/editing/rs_annotation_controller.{h,cpp}`: minimal app-side wiring of vendored annotation map tools (create/modify/select) against a `QgsAnnotationLayer` owned by the project — no vendored edits; documented as the annotation surface.
- Tests `tests/test_edit_snapping.cpp` (snap resolution known-answer on a synthetic grid layer), `tests/test_edit_validity.cpp` (bow-tie polygon → issue; repairPreview fixes; apply is explicit; invalid input negative), `tests/test_edit_sample_tools.cpp` (synthetic `QgsMapMouseEvent` drives brush/erase on offscreen canvas; undo collapses stroke to one command; erase semantics; locked-layer refusal).

## Phase 3 — Packages D+E: ROI semantics + large-editing
- `src/app/editing/rs_roi_semantics.{h,cpp}`: given a session ROI polygon + target raster layer →
  - CRS transform (layer CRS → raster CRS via `QgsCoordinateTransform`, fail-closed on failure — lesson from issue #1005, applied in-scope);
  - pixel footprint via `RsPixelRasterizer` (center-of-pixel, windowed);
  - NoData/edge: clip to raster extent, NoData-masked valid-pixel count reported separately;
  - stats preview: per-band min/max/mean/std over valid pixels, cancelable (`std::function<bool()>`), bounded (early exit > N pixels unless opt-in);
  - class label/attributes write-back into sample layer feature (edit-command wrapped).
- `src/app/editing/rs_edit_index.{h,cpp}`: `QgsSpatialIndex` maintained over an attached layer (bulk build bounded + incremental on `featureAdded/featureDeleted/geometryChanged`), `intersects(box)`, `nearest(point,n)`; selection-bounds helper; **no full UI rebuild** — consumers query the index, canvas updates via layer's own changed-signal scope.
- Tests `tests/test_edit_roi_semantics.cpp` (known-answer footprint count on synthetic raster, CRS-shift answer, NoData edge, cancel, class write-back), `tests/test_edit_index.cpp` (100k-feature logical scale build via bounded generator + query answers vs brute force on subset; incremental add/remove; selection bounds).

## Phase 4 — Packages F+G: agent surface + persistence + integration
- `src/app/editing/rs_edit_agent_tool.{h,cpp}`: read-only `SpatialTool` id `editing:state` exposing session facts (layers in edit, dirty, undo/redo depth, selection counts, snapping enabled, validity issue count). Registered via `SpatialToolRegistry` following the `WorkbenchContextTool` injection pattern with `QPointer` guard. **No write operations exposed to agents** (envelope: write ops stay explicit app commands; prevents bypassing undo/permission).
- `src/app/editing/rs_edit_persistence.{h,cpp}`: `exportLayerToGeoJson` / `exportLayerToGpkg` via `QgsVectorFileWriter` to temp file + atomic rename; commit-with-report wrapper for the session; Unicode-path safe (QString paths throughout); failure cleanup of temp files.
- Integration wiring (append-only): session/tools mounted in `setupWorkbenchInfrastructure()` next to `WorkbenchContextTool`; tool provider prefix group for `editing:` (one-line addition in the existing group map); menu action only if trivially appendable (else document as follow-up).
- Tests `tests/test_edit_agent_state.cpp` (schema known-answer, stale-window behavior, no-write-surface negative: registry exposes zero mutable editing tools), `tests/test_edit_persistence.cpp` (GeoJSON round-trip known-answer, atomic rename no-partial-file negative, Gpkg export + reopen read-back).

## Phase 5 — hardening
- Failure matrix pass: layer deleted mid-command-guard, project closed with dirty session, read-only source layer (gpkg on read-only file?), invalid CRS transforms, cancel paths. Fix + `FAILURE_MATRIX.md`.
- Resource: assert bounded memory in index bulk build (logical cap env `RS_EDIT_INDEX_MAX_FEATURES`, default 100k, opt-in higher); stroke merging caps; stats early-exit.

## Phase 6 — E2E + docs
- `tests/test_editing_e2e.cpp`: offscreen canvas end-to-end — draw sample polygon with brush → validity check → transform → stats → commit → export GeoJSON → reload into new memory layer → facts equal; layer deletion and project close during session.
- `docs/workbench/editing-platform.md`: authority model, contracts (session facts schema, tool behavior, semantics), failure semantics, agent surface.
- `CHANGELOG.md` entry (Unreleased section, existing bullet style).

## Phase 7 — adversarial review
- Main-agent full-diff review, then subagent #2 read-only adversarial review of `origin/master...HEAD`. All P0/P1 fixed; P2 fixed or dispositioned; P3 recorded. Re-run targeted gates.

## Phase 8 — final
- Rebase `origin/master`; conflict markers/secret/`git diff --check` scans; targeted tests **twice consecutively**; push branch; create PR with full PR_BODY.md. No merge, no CI wait.

## Out of scope this slice
- Vendored QGIS core/gui modifications (none planned; annotation GUI classes consumed as-is).
- Rewriting `main_window_vector.cpp` legacy flows (checkUnsavedChanges dialog) — the session layers *alongside*; migration of legacy call sites is a follow-up.
- Open issues #1001–#1007 (other domains; see BASELINE dedupe).
- PR #1008 spectral business files.
