# BASELINE — qgis-editing-annotation-11

Recorded: 2026-09-15 (start of track, after read-only Phase 0 audit of origin + GitHub + code)

## Git / GitHub state at track start

| Item | Value | Evidence |
| --- | --- | --- |
| Baseline `origin/master` | `a5b11b7f10fa010c1c060864fb427d777ba9a4aa` ("fix: fail-closed fixes for review issues #994–#999 (#1000)") | `git rev-parse origin/master` after `git fetch origin --prune` |
| Prompt-snapshot SHA | `ebcafb4d02…` — **stale**; #991 (D18 workbench) and #992 (D19 foundry) have been merged, their remote branches deleted (confirmed by fetch prune output) | fetch output |
| Open PRs | Only **#1008** `zcode/radiometric-spectral-workbench` — "feat(spectral): Day 13 radiometric calibration, 6S atmospheric correction & spectral workbench", base master, `mergeStateStatus: CONFLICTING`, not draft | `gh pr list --state open` |
| Open issues | #1001–#1007, all R2-review findings (`needs-triage`), P1/P2 severity, domains: dataset (#1003, #1004, #1007), workflow (#1002, #1006), georef (#1005 `mapPickToLayerCrs` fail-open), io (#1001 `io:clip` CRS override misuse) | `gh issue list --state open` |
| Remote branches (non-ITK) | `origin/master`, `origin/zcode/radiometric-spectral-workbench` only | `git branch -r --sort=-committerdate` |
| `ISSUES.md` | Old D3 lab-content operator-gap backlog (temporal/SAR/hyperspectral/cartography operators, T-1…C-2). Not an editing backlog; several entries retired per later tracks. **Not implemented from.** | `sed -n '1,260p' ISSUES.md` |
| ADR numbering | Highest = `docs/adr/0162-workflow-ir-v2-and-dag-engine.md`; next free **0163** | `ls docs/adr \| tail` |

## Issue dedupe (rule: open issues must not be silently ignored, must not be double-implemented)

| Issue | Domain | In this track's primary scope? | Disposition |
| --- | --- | --- | --- |
| #1001 io:clip CRS override | src/processing/io | No | OUT_OF_SCOPE (owned by io/process track); not touched |
| #1002 registry node executor fail-open | src/workflow | No | OUT_OF_SCOPE |
| #1003 joinFeaturesBySampleId JSON-null | src/dataset (D19) | No | OUT_OF_SCOPE (D19 internals are read-only for this track) |
| #1004 dataset:qa identity scan_capped | src/agent sample tools | No | OUT_OF_SCOPE |
| #1005 mapPickToLayerCrs untransformed canvas point (fail-open) | src/app georeferencer (D14) | Adjacent (map-pick → layer-CRS semantics) but **different authority** (georef GCP workflow, not editing session). OUT_OF_SCOPE; our own map-to-layer transforms must be fail-closed and are tested independently | OUT_OF_SCOPE, lesson recorded |
| #1006 PipelineRunCoordinator syntheticExecute | src/workflow | No | OUT_OF_SCOPE |
| #1007 dataset:qa CRS audit | src/dataset | No | OUT_OF_SCOPE |

None of the open issues intersect `src/app/editing/**` (which does not exist yet), `src/gui/**maptool**`, or `src/analysis/**roi**`.

## Parallel ownership at start

Only open PR #1008 (spectral/radiometric). Its changed files overlap this track **only in shared integration files** (`tests/CMakeLists.txt`, `src/app/CMakeLists.txt`, `src/analysis/CMakeLists.txt`, `src/core/CMakeLists.txt`, `src/agent/CMakeLists.txt`, `.gitignore`). Business files are disjoint. Policy: append-only minimal integration diffs; never touch its business files. See `PARALLEL_OWNERSHIP.md`.

## Toolchain on this box

| Item | Value | Evidence |
| --- | --- | --- |
| Configure preset | `dev-default` → `binaryDir ${sourceDir}/build-dev`, Debug, `ENABLE_TESTS=ON` | `CMakePresets.json` |
| Generator (existing cache) | `Unix Makefiles` | `grep CMAKE_GENERATOR build-dev/CMakeCache.txt` (main repo) |
| Build invocation | `cmake --build build-dev --target <t> -j2` (hard cap -j2; drop to -j1 under pressure) | GOAL envelope |
| Tests | Catch2; `sicnu_add_test(NAME)` / `sicnu_discover_tests(NAME)` helpers in `tests/CMakeLists.txt`; offscreen via `set_tests_properties(... ENVIRONMENT "QT_QPA_PLATFORM=offscreen")` | `tests/CMakeLists.txt:26-146` |

## Existing authorities / seams on master this track builds on (not re-implements)

| Authority | Location | What it gives |
| --- | --- | --- |
| `QgisDesktopWindow` (main app window) | `src/app/main_window*.cpp` | editing actions `undo()/redo()/cutFeatures()/pasteFeatures()/checkUnsavedChanges()` (`main_window_vector.cpp:44-159`), vector layer add (`:306` addFeature tool) |
| `MapToolManager` | `src/app/map_tools/map_tool_manager.{h,cpp}` | owns full QGIS-native vector toolset: add feature, move/rotate/scale, offset curve, reshape, split features/parts, simplify, reverse line, add ring/part, fill ring, delete part/ring, trim/extend, chamfer/fillet, feature array, vertex tool |
| `src/app/vertextool/` | full QGIS vertex tool port (`qgsvertextool.{h,cpp}`, `qgsvertexeditor.*`, `qgslockedfeature.*`) | interactive vertex editing |
| QGIS gui maptools (vendored, full impl) | `src/gui/maptools/` (e.g. `qgsmaptoolcapture.cpp` 2204 lines, upstream GPL header) | base classes; **no stub markers** in maptools dir |
| QGIS annotation GUI (vendored, compiled) | `src/gui/annotations/` (10 classes incl. `qgscreateannotationitemmaptool`, `qgsmaptoolmodifyannotation`, `qgsmaptoolselectannotation`) compiled via `src/gui/CMakeLists.txt:125-134` | **zero app-level consumers** (`grep -rn QgsAnnotation src/app` → none) — genuine gap |
| ROI digitizing tools | `src/app/classification/rs_roi_tool_{base,point,rectangle,polygon,freehand,magicwand}.*` — emit `roiDrawn(QgsGeometry,int classId)`; receiver rasterizes via `RsPixelRasterizer` | existing sample-drawing surface (classification workbench) |
| `RsPixelRasterizer` | `src/analysis/classification/rs_pixel_rasterizer.h` | canonical pixel-membership (center-of-pixel) rasterization |
| `RsRoiLabeler` | `src/analysis/segmentation/rs_roi_labeler.h` | ROI-majority segment labeling, fail-closed + cancel |
| `RoiStatisticsWidget` | `src/app/widgets/roi_statistics_widget.{h,cpp}` | per-band stats preview with epoch-cancel + bounded pool (#625/#797 patterns) |
| `SpatialTool` / `SpatialToolRegistry` | `src/agent/spatial_tools/spatial_tool.h:82-157`, registry singleton | agent tool surface (name `ns:tool`, JSON schema, `SpatialToolResult`) |
| `WorkbenchContextTool` pattern | `src/app/workbench/agent_context_tool.{h,cpp}`, registered `src/app/main_window_workbench.cpp:578-612` | read-only SpatialTool with injected payload provider + QPointer guard — the precedent for agent awareness |
| `MissionContext` (D18, merged) | `src/app/workbench/mission_context.h:100-129`, store `mission_context_store.h:28-56`, window member `main_window.h:588` | selection/mission facts already published; editing facts are NOT part of it |
| `IWorkbench::isDirty()` | `src/app/workbench/workbench_host.h:53-93` | existing dirty-state hook |

## Verified gaps this track targets (each with evidence)

1. **No editing-session authority.** Edit state is ad-hoc: `checkUnsavedChanges()` scans *all* project layers with a QMessageBox (`main_window_vector.cpp:44-109`); `undo()/redo()` only act on the *active* layer (`:110-126`) with no aggregate state, no lock, no lifecycle guard against layer removal mid-edit. → Package A.
2. **No snapping configuration anywhere in the app.** `grep -rn "SnappingUtils\|snapping" src/app/main_window*.cpp src/app/map_tools/*.cpp` → empty. → Package C.
3. **No geometry validity feedback path.** No app-level use of `QgsGeometry::validateGeometry`/makeValid preview (grep). Digitized self-intersecting polygons are committed silently. → Package C.
4. **No brush/erase sample-painting tools.** Existing ROI tools are point/rectangle/polygon/freehand/magicwand only (`ls src/app/classification/rs_roi_tool_*`). → Package B.
5. **Annotations unreachable from the app.** Compiled in gui lib, zero app consumers. → Package B (wire minimal create/modify/select surface in our own layer, no vendored edits).
6. **ROI↔raster semantics scattered.** Rasterization + stats exist for the classification workbench, but there is no editing-side contract for pixel footprint, raster-CRS transform, NoData-aware counts, or cancelable bounded preview attached to an edit session. → Package D.
7. **No agent-visible editing state.** `spatial_tool_provider.cpp:39-47` id-prefix groups contain no editing namespace; nothing publishes edit-session facts. → Package F.
8. **No atomic-save/interchange helper for edited layers.** `commitChanges()` results are ignored in `checkUnsavedChanges()` (`main_window_vector.cpp:66` — return value discarded; commit errors are silent). → Package G.

## Rescope decisions (from "why this track now" → actual gaps)

Prompt's Package B listed "point/box/polygon/freehand/brush/erase/split/reshape". Audit: point/box/polygon/freehand ROI tools **exist** (classification); split/reshape **exist** (MapToolManager QGIS-native). Net-new in B: **brush/erase sample tools** + **annotation wiring**. Packages re-centered accordingly (see PLAN.md); no re-implementation of existing tools.
