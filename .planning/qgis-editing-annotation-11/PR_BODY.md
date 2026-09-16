## feat(editing): QGIS Editing, Annotation & ROI Platform 11.0 (F11)

Baseline: origin/master @ `a5b11b7f10fa010c1c060864fb427d777ba9a4aa`.
Branch `zcode/qgis-editing-annotation-11`, worktree `../exp-rs-qgis-editing-annotation-11`.
**Local evidence only; no online CI dependency.** Do not merge without reviewing the P0 out-of-scope note below.

## P0 (out of scope, pre-existing on the baseline)

The `sicnu_geo_rs` app target **does not compile on master `a5b11b7f10` itself**: `src/workflow/workflow_ir_v2.h:97` and `src/workflow/workflow_types.h:47` both define `struct sicnu::workflow::WorkflowDefinition`, so every TU whose include chain sees both fails with a redefinition error. This has never been caught locally because the main repo's build-dev predates the D17/D19 merges. Evidence: compiling the **pristine** `origin/master` versions of `main_window_view.cpp` / `main_window_workbench.cpp` with this build's exact flags fails with the identical errors (logs referenced in `EVIDENCE.md → OUT_OF_SCOPE`). Repair means renaming/reshaping a workflow-domain type across 14+ files — out of scope for this track and deliberately not done here. Consequence for this PR: the app target cannot link on the baseline; this track's integration TU compiles standalone with the app's exact flags (exit 0), and all 10 new test targets build and pass.

## Dedupe / ownership vs. parallel work

- Start-time audit: only open PR was #1008 (`zcode/radiometric-spectral-workbench`, spectral/radiometric domain) — business files disjoint; shared integration files (`tests/CMakeLists.txt`, `src/app/CMakeLists.txt`, `.gitignore`) received append-only minimal diffs at distinct locations. Issues #1001–#1007 (R2 findings in io/workflow/dataset/georef domains) deduped as out of scope (table in `BASELINE.md`).
- The prompt-snapshot PRs #991/#992 were already merged at start; their domains (MissionContext, dataset foundry) are master authority — read, not re-implemented.

## What is delivered (packages A–H)

All business code is new `src/app/editing/` (app-level authority over edit STATE/LIFECYCLE — `QgsVectorLayer` edit buffers stay the geometry truth; vendored QGIS untouched):

- **A `RsEditSession`** — aggregated per-layer edit facts (editing/modified/locked/undo-redo depth/feature+selection counts; O(1) incremental `featureCount`), undo/redo delegation, commit/rollback that **reports** failures (legacy `main_window_vector.cpp:66` discarded `commitChanges()`'s result), locks that never block save paths, `layerWillBeRemoved`-safe lifecycle; RAII `RsEditCommandGuard` (one guard = one undo step).
- **B tools** — `RsSampleBrushTool`/`RsSampleEraseTool` (disc-stamp sample painting: one stroke = one edit command, bounded stamps with coalescing, layer-CRS-unit radius, explicit refusal contract incl. single-part targets); `RsAnnotationController` (first app consumer of the vendored annotation toolset: project-owned layer reused on reopen, programmatic items, interactive create/modify/select tools). Existing point/box/polygon/freehand/split/reshape tools on master were deliberately **not** re-implemented.
- **C snapping/validity** — `RsSnappingController` (single writer of the canvas `QgsSnappingUtils` config — the app never configured snapping before) and `RsGeometryValidity` (structured issues + `makeValid` **preview**; repairs never applied implicitly).
- **D `RsRoiSemantics`** — fail-closed ROI→raster CRS transform (in-scope application of the fail-closed lesson from issue #1005), windowed center-of-pixel footprint via the existing `RsPixelRasterizer`, NoData-aware `pixelCount`/`validPixelCount`, cancelable per-band stats (two-pass stddev) with a fail-closed pixel budget, edit-command class write-back.
- **E `RsEditIndex`** — `QgsSpatialIndex` bulk-build (bounded, cancelable, `RS_EDIT_INDEX_MAX_FEATURES` cap) maintained incrementally across add/delete/geometry-change incl. undo; `intersects/nearest/bounds/selectionBounds` never scan the layer or rebuild UI.
- **F agent surface** — read-only `editing:state` SpatialTool (catalog group `editing`) exposing session/snapping facts; **no agent write path exists** (verified behaviorally); wiring follows the `WorkbenchContextTool` precedent (+19 lines in `main_window_workbench.cpp`) and the provider gains the `editing:` prefix (+4/−1).
- **G `RsEditPersistence`** — GeoJSON/GPKG export through `QgsVectorFileWriter` + its `newFilename` out-param, POSIX-atomic rename over the target, failure-path temp cleanup, suffix-based driver selection (refuses unknown formats), `commitReported` per-layer error collection.
- **H tests** — 10 offscreen Catch2 suites (`test_edit_*`, ~2,600 lines) with independent oracles: a-priori GDAL-written value grids, hand-computed center-of-pixel counts (boundary-free ROI), brute-force index verification at the 100k logical scale, core-level `setAllowCommit` commit-failure injection, JSON re-parse round-trips, synthetic `QgsMapMouseEvent` drives, and a composed interaction E2E (draw→validate→stats→label→undo/redo→commit→export→lifecycle→agent facts).

## Compatibility

Additive only. No vendored QGIS edits; no existing symbol renamed; memory-provider behavior unchanged; the two fixed baseline breaks are 1–2-line, behavior-neutral unblocks (see below). Consumers of `SpatialToolProvider` see one new read-only tool in a new group.

## Review findings + dispositions (full table in `REVIEW_LOG.md`)

Independent adversarial review verdict at review time: P0=0, P1=2, P2=4, P3=8 → **P1 2/2 fixed** (two-pass stddev for DN grids; POSIX-atomic rename), **P2 3 fixed + 1 documented** (mounted session starts empty — attach wiring is a follow-up), **P3 6 fixed + 2 dispositioned**. All fixes re-verified by the suite.

## Local tests / resource evidence

- `ctest --test-dir build-dev -R "test_edit" -j1` with `QT_QPA_PLATFORM=offscreen` → **10/10 passed, twice consecutively** (final runs 9.34 s / 9.28 s).
- Build: fresh `build-dev` via `cmake --preset dev-default` (offline Catch2 reused via `FETCHCONTENT_SOURCE_DIR_CATCH2`), all compiles at the hard cap `-j1/-j2` (16-core host shared with a concurrent track; sampled load 15–21, memory ≤19%; periodic 60 s sampling not-executed for detached builds — recorded in `PERFORMANCE.md`).
- `git diff --check` clean; conflict-marker scan clean; no secrets; no network access at runtime.

## Known limitations / follow-ups

1. App-target build blocked by the pre-existing workflow break (P0 note above) — repair suggestion: rename the v2 `WorkflowDefinition` (or legacy) and migrate its users; proposed owner: a workflow-domain track.
2. Legacy `main_window_vector.cpp` dialogs not yet routed through the session; `MapToolManager` start/stop wiring for the mounted session (until then the agent surface reports `layers: []` in a fresh app run).
3. Annotation undo not supported (QGIS annotation layers have no edit buffer); brush cap/coalescing path covered by inspection only; commit-failure injection beyond `setAllowCommit` not attempted.

## Out-of-scope findings

See `EVIDENCE.md → OUT_OF_SCOPE` (issues #1001–#1007 dispositions, PR #1008 ownership, the workflow P0 above).
