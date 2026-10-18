# R16-D Work Packages (Phase 1 decomposition)

Sources: two independent read-only audit reports against master @ 46791bc65 (T07: 11 verified findings + 12 checked-and-rejected; T08: 12 verified findings + 13 checked-and-rejected). Full finding text preserved in track ledgers. Each WP has an executable oracle: RED test first, fix, GREEN×2.

## T07 (Qt/QGIS workbench & layer-edit state)

| WP | Finding | Sev | Files | Oracle |
|----|---------|-----|-------|--------|
| WP-T07-1 | F01+F02: edit lease acquired-then-instantly-released (exclusivity inert, commitEdit always `no_edit_lease`, stale layers); relocateLayer destroys layers holding uncommitted edit buffers (UAF once lease held) | P1(+P0 combo) | main_window_vector.cpp, main_window.h, qgis_display_manager.cpp | test_r14_D_vector_edit_honesty: hasActiveEditLease true during edit; conflict path on 2nd view; commit advances revision; saveEdits survives relocation. test_qgis_display_manager: relocate skips editable layers |
| WP-T07-2 | F03: disabled-without-reason — updateEditingUI bare setEnabled; ribbon addActionButton clobbers accessibleDescription with static tooltip (clears it when enabled) | P1 a11y | main_window_vector.cpp, ribbon_controller.cpp | honesty test: disabled editing action exposes a "why"; enabled action keeps its descriptive accessibleDescription |
| WP-T07-3 | F04: 13 digitizing tools enabled regardless of geometry type | P2 | main_window_vector.cpp | point layer edit mode → OffsetCurve/AddRing disabled; line → AddRing disabled; polygon → Reverse disabled |
| WP-T07-4 | F05: ribbon band combos + brightness/contrast mutate FALLBACK first-raster when current layer is not raster | P2 | ribbon_controller.cpp | offscreen: current=vector → renderer of raster unchanged |
| WP-T07-5 | F06: setLayerVisible success when tree node missing (fake success) | P2 | qgis_display_manager.cpp | test: node removed → typed failure |
| WP-T07-6 | F07: `workbench:context` declared read-only but provider writes m_mission + ensureMissionId | P2 | main_window_workbench.cpp (lambda), agent_context_tool.cpp docs | test: execute() leaves mission id absent |
| WP-T07-7 | F09+F10: StdDev stretch bandStatistics without 250k sample cap; PiecewiseLinearEnhancement O(n) scan → binary search | P3 perf | qgs_display_stretch.cpp, piecewise_linear_enhancement.cpp | test_display_stretch: stub provider receives bounded sampleSize; enhance() value-equal to linear reference |
| WP-T07-8 | F11: georef dual window hardcoded Qt::white cells + "RMSE: 0.0000 px" when no fit | P3 | georef_dual_window.cpp | label shows no-fit state, palette-derived brush |
| Deferred | F08: magic-wand full-raster read (latent, no production canvas hook) | P2 latent | classification_studio_widget.cpp | NOT fixed this round — evidence recorded, no reachable production path today; listed in PR as open item |

## T08 (MapSpec + cartography quality)

| WP | Finding | Sev | Files | Oracle |
|----|---------|-----|-------|--------|
| WP-T08-1 | F03: chart_registry unguarded asString() on user JSON (colorbar min/max, data labels, palette entries) → uncaught Json::LogicError through compile | P1 crash-class | chart_registry.cpp (+validation) | REQUIRE_NOTHROW(compile) with numeric colorbar min/max; typed validation problems |
| WP-T08-2 | F04: atlas png export exports ALL pages per feature → sibling `*_2.png` orphans, delivery corrupt | P1 | export.cpp | atlas spec with pages[] ≥2 → delivery dir exactly manifest-listed files |
| WP-T08-3 | F02: inset extent validation dead code (`"insets"` vs actual `"inset_maps"`) → CARTO-6 class silent fallback | P1 | mapspec.cpp | crossed/NaN inset extents produce validation problems |
| WP-T08-4 | F06: vector scaledenominator min/max ordering unvalidated → never-visible layer, report says applied | P1 | style_spec.cpp | validateStyleSpec produces ordering problem like raster sibling |
| WP-T08-5 | F05: series clones don't remap overlay_on → false MAP_CHART_OVER_MAP per page | P2 | series_planner.cpp | clone overlay_on == remapped frame id; preflight clean |
| WP-T08-6 | F07: anchors + MAP_OFF_PAGE/MAP_MARGIN use page-0 dims; compiler honors per-page sizes | P2 | composition.cpp, quality.cpp | 2-page spec: item off smaller page-1 flagged; anchor resolves vs page-1 dims |
| WP-T08-7 | F08: structuralDigest ignores pages[] geometry | P2 | quality.cpp | mutate pages[0].height_mm → digest changes |
| WP-T08-8 | F09: vector series over point layer → degenerate extent → VALIDATION_FAILED on planner output | P2 | series_planner.cpp | point-layer planSeries → validateMapSpec empty |
| WP-T08-9 | F01: condition tokenizer accepts `1.2.3` as 1.2 silently | P2 | mapspec_conditions.cpp | validateConditionSyntax("a > 1.2.3") false |
| WP-T08-10 | F10: series index label hardcoded A4-landscape rect → MAP_OFF_PAGE on portrait templates | P3 | series_planner.cpp | portrait template → zero preflight errors |
| WP-T08-11 | F12: searchCores per-target budget dead code | P3 | composition.cpp | two disjoint conflicts → each core ≥1 simulation |
| Deferred | F11: produce runs full composition solve twice per iteration + re-renders chart PNGs per compile | P2 perf | produce.cpp, mapspec_compiler.cpp | NOT fixed this round — needs a pre-composed seam across produce↔compile boundary; risk of semantic drift outweighs budget; recorded as open item |

Priority order: all P1s first (T08 WP1-4, T07 WP1-2), then P2s, then P3 quick wins. Deferred items documented, not silently dropped.
