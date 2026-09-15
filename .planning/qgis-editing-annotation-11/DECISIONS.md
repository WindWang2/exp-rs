# DECISIONS — qgis-editing-annotation-11

Autonomy=full: every ambiguity resolved here with the most conservative, least-duplicating option. D-numbering is local to this file.

## D1 — Baseline refresh: #991/#992 merged, only #1008 open
Prompt snapshot listed #991/#992 as open; at start they are merged into `a5b11b7f10`. Consequence: MissionContext/`src/app/workbench/**` and D19 dataset internals are now **master authority**, not parallel-track territory. But `main_window_*` / workbench files remain **minimal-append-only** for this track anyway (they are broad shared files; the parallel #1008 does not touch them, and smallest integration diff wins). Reading MissionContext APIs from master is allowed and done (agent tool overlays facts).

## D2 — All business code in new `src/app/editing/`; zero edits to vendored QGIS
GOAL's write scope mentions `src/gui/**maptool**`, but envelope default reads vendored QGIS as read-only. Chosen: `src/app/editing/**` only; new tools subclass `QgsMapTool`/consume `src/gui/annotations` classes as-is. Rejected alternative (patching vendored gui) would expand conflict surface with zero capability gain.

## D3 — Package B rescope: build brush/erase + annotation wiring; do NOT re-make point/box/polygon/freehand/split/reshape
Audit evidence (BASELINE.md §gaps): ROI point/rect/polygon/freehand/magicwand exist (`src/app/classification/rs_roi_tool_*`); split/reshape/simplify etc. exist as QGIS-native tools in `MapToolManager`. Re-implementing them under new names is forbidden duplication (envelope). Net-new B deliverables: sample **brush/erase** tools (RS painting gap) + **annotation controller** (compiled-but-unreachable gui capability). Recorded here per "rescope 写入 DECISIONS.md".

## D4 — EditSession is the new authority; legacy `main_window_vector.cpp` flows stay, wired alongside
Two candidates: (a) rewrite legacy edit actions onto the session immediately; (b) introduce `RsEditSession` as the authority + facts source, mount it centrally, leave legacy dialogs functioning. Chosen (b): conservative, avoids UI-behavior regressions in shared `main_window_*` files while #1008-era refactor churn is a risk; migration of legacy call sites listed as follow-up. The session is usable headless (tests, agent facts) without any dialog.

## D5 — Snapping via `QgsSnappingUtils`/`QgsSnappingConfig` on the canvas, not a custom snapper
QGIS canvas already owns `snappingUtils()` with project-backed config. A second snapping engine would be a forbidden second truth. Controller only configures + reports.

## D6 — Validity: report + preview + explicit apply; never silent mutation
`RsGeometryValidity::repairPreview()` returns a `makeValid`ed candidate geometry; the caller decides. Rationale: GOAL Oracle #2 ("几何 validity 问题不静默吞掉") and envelope ("不静默改几何"). Issues are structured (type,所在 vertex/segment info where QGIS provides it), never swallowed.

## D7 — Brush semantics: one stroke = one edit command; stamp = disc polygon in layer CRS
Brush drag creates per-move disc stamps buffered in layer CRS; on release they merge into a single `QgsGeometry::combine`d part set added under ONE `beginEditCommand` so undo collapses the whole stroke (Oracle #1). Erase removes features intersecting the stamp (whole-feature semantics; split-on-erase rejected as scientifically surprising for samples). Stamp derived from `QgsDistanceArea`-free planar buffer in layer CRS units; tools refuse non-projected/degree CRS for brush radius>0 in degrees? — no: radius is interpreted in **layer CRS units** and documented; test uses a metric CRS. Conservative and unit-honest.

## D8 — Agent surface read-only (`editing:state`), no agent write tools
Envelope F: "只暴露稳定 edit state/selection facts，写操作仍走明确 command". The tool exposes facts only; it has no `execute` paths that mutate anything. Registration follows `WorkbenchContextTool`'s injected-provider + QPointer-guard pattern (`main_window_workbench.cpp:578-612`). New id prefix `editing:` added to the provider group map (one-line, additive; provider currently filters known prefixes).

## D9 — Pixel footprint = center-of-pixel via `RsPixelRasterizer` (existing analysis authority)
No second rasterizer. NoData/edge: geometry clipped to raster extent; valid-pixel count excludes NoData via band read; counts reported separately (`pixelCount` vs `validPixelCount`). CRS transform fail-closed (returns error rather than passing raw coords — lesson from out-of-scope issue #1005).

## D10 — Large-editing scale gate is logical + opt-in
100k features built by generator in-process; query correctness checked against brute force on a bounded subset; memory assertions avoided (no RSS gate in unit test) — scale evidence is operation counts + invariant checks. Higher scales via env opt-in (`RS_EDIT_INDEX_MAX_FEATURES`). No wall-clock assertions (envelope).

## D11 — Persistence: `QgsVectorFileWriter` → temp + `Qt`-safe atomic replace
Export writes to `<target>.tmp-<pid>` then renames over target; temp cleanup on every failure path; QString paths (Unicode-safe). GPKG driver used for `.gpkg`. Commit-with-report wrapper collects per-layer commit errors into strings instead of ignoring the bool (`main_window_vector.cpp:66` bug class).

## D12 — Test naming/registration: `test_edit_*` via `sicnu_add_test` style manual blocks
Repo helper `sicnu_add_test` links many sicnu libs not needed here; several existing tests use explicit `add_executable` blocks (e.g. `test_spectral_profile_widget` lines 2739-2757). Chosen: explicit blocks mirroring `test_roi_tool_polygon`/`test_swipe_map_tool` (Qt6 Core/Gui/Widgets + qgis_core/gui + only the app TUs under test, AUTOMOC ON), `TEST_PREFIX` for the family gate, `QT_QPA_PLATFORM=offscreen` per-test properties. Keeps link-surface minimal and compile fast under -j2.

## D13 — Offscreen event synthesis
Follow `tests/test_roi_tool_polygon.cpp:20-58` precedent: one heap QApplication per process (`ensureApp()`), synthetic `QgsMapMouseEvent` into `canvasReleaseEvent/canvasMoveEvent`, `QSignalSpy` on tool signals; `QgisFixture` RAII (`test_spectral_profile_widget.cpp:24-37`) where QgsApplication needed.

## D14 — ADR
This track records its authority model in `docs/adr/0163-editing-session-authority.md` (next free number, audited) and the user-facing contract in `docs/workbench/editing-platform.md` (new dir — GOAL-specified deliverable path).
