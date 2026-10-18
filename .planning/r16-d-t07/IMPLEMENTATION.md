# R16-D-T07 Implementation Report

Base: origin/master 46791bc65 · branch hardening/r16-d-t07-workbench. Full evidence chain in GOAL_LOOP_LEDGER.md (RED on master incl. two live SIGSEGV demonstrations of the P0 class, then GREEN×2).

## Per-WP changes

| WP | Files | What was wrong | What changed |
|----|-------|----------------|--------------|
| T07-1 (P1/P0) | main_window.h, main_window_vector.cpp, main_window_connections.cpp, qgis_display_manager.cpp | `acquireEditLease` let the RAII `AssetLease` die at return: exclusivity inert (2nd view "acquires"), `commitEdit` always `no_edit_lease` (revision never advanced → stale sibling layers), conflict dialog dead code. Once held, `assetChanged→relocateLayer` would destroy the editing layer mid-`saveEdits` (UAF — reproduced as SIGSEGV in RED) and destroy layers with buffered edits on any asset change | Leases held in `m_editLeases` (keyed by QGIS layer id); every commit/rollback path routes through the lease (incl. `confirmSaveEdits`/`checkUnsavedChanges` which previously bypassed it); `layerWillBeRemoved` drops a removed layer's lease; `relocateLayer` DEFERS swaps of editable layers (`display.relocate_deferred_editing`, retried on next assetChanged, dropped on remove); `saveEdits`/`toggleEditing`/`confirmSaveEdits` snapshot the layer id (+ displayLayerId) and re-resolve after lease ops, re-pointing the session at the relocated replacement |
| T07-2 (P1 a11y) | main_window_vector.cpp, ribbon_controller.cpp | 13 digitize actions + Save: bare `setEnabled`, no reason; ribbon `addActionButton` published the static usage tooltip as the "reason" and CLEARED accessibleDescription when enabled | `setActionUnavailableReason`: reason appended to tooltip (base kept restorable in `sicnu/baseToolTip` property) + `sicnu/unavailableReason` property; ribbon sync keeps `accessibleDescription` meaningful in BOTH states |
| T07-3 (P2) | main_window_vector.cpp | All 13 tools enabled for ANY editable layer — enabled-but-will-fail (offset/reverse on points, rings on lines/points) | Per-action geometry masks; reasons state the required geometry |
| T07-4 (P2) | ribbon_controller.cpp | `currentRasterLayer()` fell back to the first canvas raster: band combos + brightness/contrast silently mutated and repainted a layer the user had not selected | Fallback removed; combos/sliders disabled (tooltips retained) when the current layer is not a raster |
| T07-5 (P2) | qgis_display_manager.cpp | `setLayerVisible` returned success with the tree node missing (request dropped, linked-visibility propagated a never-applied state) | Typed failure `display.layer_tree_node_missing` |
| T07-6 (P2) | main_window_workbench.cpp | `workbench:context` (declared read-only) wrote `m_mission` + `ensureMissionId` on every read | Projection stays local; session mission untouched by reads |
| T07-7 (P3 perf) | qgs_display_stretch.cpp, piecewise_linear_enhancement.cpp | StdDev branch + both `QgsBandStatsSource::stats` calls ran uncapped full-band scans per Apply (#634 capped only bandRange/histograms); piecewise enhance was an O(points) linear scan per pixel (~257 segments on float rasters, no LUT) | `sampleSize=250000` on all three calls; `std::lower_bound` segment lookup, value-equivalent (6109-assert equivalence guard) |
| T07-8 (P3) | georef_dual_window.cpp | Hardcoded `Qt::white` residual cells (glare under dark theme); "RMSE: 0.0000 px" with no fit | `palette().base()` brush; "RMSE: — (no fit)" until a fit exists |

## Honest oracle notes (what could NOT be directly asserted and why)
- WP-4: no test target constructs RibbonController standalone; combos/sliders are private. All mutation paths flow through `currentRasterLayer()` — the single seam whose fallback was removed; family suites cover window-level sync. Seam-verified, not widget-asserted.
- WP-6: the mutating lambda lives behind the shell's tool-registry registration; the workbench unit target cannot execute it. The write was removed outright; the tool docstring already promises read-only.
- WP-8: no registered test target drives GeorefDualWindow; two-line change, code-review-verified.
- WP-7 sample caps: behavior-preservation covered by the stretch families; the 250k argument itself is not asserted (would need a provider spy — noted, not fabricated).

## Deferred (evidence, not fixed)
- T07-F08 `RsRoiMagicWandTool::extractRegion` full-raster read (classification_studio_widget.cpp:46-74): latent — only test-reachable today (no production canvas hook); left for the studio canvas wiring owner.
