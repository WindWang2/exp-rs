# R27-T04 Baseline

- **BASE_SHA**: `10652fd003c66ae78598d2b878eed9beb50029fa` (= origin/master at fetch time)
- **Fetch/UTC**: 2026-11-02T16:24:58Z (`git fetch origin --prune`, no new refs)
- **Worktree**: `../exp-rs-r27-t04-20261102T162559Z`, branch `agent/r27-t04-qt-qgis-workbench-ui-20261102T162559Z`
- **Main repo state**: clean (`?? .workbuddy/` untracked only); local master `2610cb4741` is an ancestor of BASE_SHA.
- **Open PRs**: none (checked `gh pr list --state open` → `[]`). No branch collision at start.
- **Sibling worktrees on host**: r25-A/B/C/D/E, r26-harness/science/workbench, r27-t01/t02/t03 — none of them own `src/app/**` GUI files I touch except r27-t03 (agent-security; owns `src/app/agent_ops/**` which I must not touch).

## Scope recap (from task)
- Write whitelist: `src/app/**` (excl. `src/app/agent_ops/**`), `src/gui/**`, `src/ui/**`, `resources/styles*.qss`, UI/platform parts of `src/native/**`. Read-only reliance on `src/geospatial/**`, `src/runtime/**`.
- #1765: I own only the frontend call site (`src/app/dialogs/stac_browser_dialog.cpp` + app entry); the safe COG/range-cache API itself is T2's (`src/geospatial/remote/**` — already on master, read-only for me).

## Key master facts verified during preflight
1. `QgsMapCanvas::unsetMapTool` (src/gui/qgsmapcanvas.cpp:2923) calls `oldTool->deactivate()` then emits; `RsRoiSpectrumTool::deactivate` (rs_roi_spectrum_tool.cpp:64-68) `deleteLater()`s when `!m_finished`; `QgisDesktopWindow::activateRoiSpectrumTool` (main_window_processing.cpp:337-343) then does a **sync `delete abandoned`** → double free (#1745 confirmed at HEAD).
2. `recomputeSpectralCurves` (qgsclassificationmainwindow.cpp:3354+) accumulates `bandSums[classId][bi] +=` where `idxByClass` keys come from `roi.classId()` (any >0) but `bandSums` vectors are sized only for `classDefs` ids → `QHash::operator[]` inserts empty vector → OOB write (#1746 confirmed). Orphan ids reachable via `rebuildRoisFromSampleLayer` (accepts any `cls_id>0`) and the merge path that drops source classDefs even when the sample-layer commit rolled back (:1097-1117).
3. `DataProjectSerializer::write` returns `workspace.downgrade_refused` failure *inside* `QgsProject::writeProject` — which cannot abort the save; `onProjectWrite` only shows a message box; `QgsProject::instance()->write()` (main_window_project.cpp:427) still overwrites the file without the SICNU block (#1747 confirmed). `QgsProject::write` emits `writeProject` at qgsproject.cpp:3397 during `write()` — preflight belongs *before* `write()` in `saveProject`/`saveProjectAsTo`.
4. STAC dialog: `onSearchCompleted` always calls `populateResults` which `setRowCount(features.size())` + `m_featureData.clear()` → pagination replaces (#1788 confirmed). More-button handler disables the button then `searchNext()`; `StacClient::searchNext` (stac_client.cpp:225-235) silently returns on stranded/no-next → button disabled forever; `search()` error paths leave prior More visible (#1789 confirmed).
5. `CrossSectionWidget::extractProfile` (cross_section_widget.cpp:30-113): sync `GDALOpen` + per-sample `GDALRasterIO` on GUI thread (#1790 confirmed). Magic wand `canvasReleaseEvent` (rs_roi_tool_magicwand.cpp:94-160): sync open + windowed RasterIO + flood fill on GUI thread (#1791 confirmed). Classification `recomputeSpectralCurves` re-opens + scanline-reads per ROI change on GUI thread (#1794 confirmed).
6. `copyPathWithDialog` (qgsclassificationmainwindow.cpp:3747-3777): `QFile::remove(dest)` before copy — dest==src deletes source (#1792 confirmed).
7. `PipelineEditorDock::readPipelineDefinitionFile` (pipeline_editor_dock.cpp:159-180): jsoncpp parse without `stackLimit` and without try/catch (#1793 confirmed). House pattern at src/app/teaching/lab_cockpit_dock.cpp:326 (`b["stackLimit"] = 256;` + typed catch).
8. `persistMissionContextWithProject` (mission_context_store.cpp:186-196): on sidecar write failure the stale `.mission.json` is **deleted** before the XML channel is persisted; if the project write then fails, both channels lose mission state (#1795 confirmed).
9. `#1561`: FIXED_IN_MASTER — commit `923595d0e2` (in master) added `isNetworkSource` + `startRemoteSampling` worker path for network-backed sources in `spectral_profile_widget.cpp`; local-source path keeps a cached dataset (acceptable per issue scope, which targets /vsicurl/ network freezing). Will not re-implement; may add a cheap regression guard test.
10. `#1765`: GUI entry (`src/app/main.cpp:144-153`) engages only the offline gate, never `sicnu::geo::network_policy::engageProcessPrivateNetworkBlocking()` — while `src/cli/main_cli.cpp:282` and the worker do. Therefore every raw `/vsicurl/` open in the GUI process (STAC COG load in stac_browser_dialog.cpp:216-221) bypasses the guarded range cache / DNS pinning. Fix at the T4-owned call site: engage the policy in the GUI entry point (existing master API, no `src/geospatial` change).

## Build environment
- ccache at /usr/sbin/ccache; ninja present (will use Unix Makefiles like siblings); preset `dev-default` (Debug, tests ON) but I configure a dedicated `build-r27-t04` with `CMAKE_C(XX)_COMPILER_LAUNCHER=cmake/raise-compiler-stack.sh;ccache` mirroring siblings.
- Build discipline: host-wide ≤4 parallel jobs; `-j2` default; flock mutex `/tmp/exp-rs-build.lock` across all my build invocations.
- Qt offscreen tests: `QT_QPA_PLATFORM=offscreen ctest -R ... -j1`.
