# TEST MATRIX — Professional Workbench 8.0

Evidence legend: COMPILED / EXECUTED-FAIL / EXECUTED-PASS / NOT-RUN (env).
All new tests: deterministic, offscreen QPA/QgsApplication, bounded fixtures
(synthetic GeoTIFF / CSV / generated JSON), no network, bounded waits.

| Area | Test file | Coverage | Status |
|------|-----------|----------|--------|
| SchemaForm 4.0 | tests/test_schema_form_4.cpp | nested objects (round-trip, nested required, optional-group absence, soft-range warnings, conditional visibility in groups, depth-cap degradation), object arrays (seeded minItems, typed round-trips, maxItems add-gate, minItems remove-gate, positional validation paths, 306-item honest truncation), dynamic enum provider (resolution, refresh with context, unknown-source free-text degradation, no-provider), async path_exists (hit/miss/supersede/teardown-drop), a11y names/descriptions | PENDING BUILD |
| AssetPreviewService | tests/test_asset_preview_service.cpp | raster gradient render + aspect fit, NoData black, typed failures (missing/garbage), vector QGIS render + typed >cap refusal, cache hit/miss provenance, supersede, cancel, dead-receiver drop, LRU entry/byte eviction | PENDING BUILD |
| Asset catalog scaling | tests/test_asset_catalog_index.cpp | index mirror/update/remove idempotence, filter by name/source/id, group single pass, panel filter box, cap+sentinel totals + non-selectable, lazy collection children (55 > 50 threshold), eager small collections, 200k-record scale evidence (filter/group < 500ms) | PENDING BUILD |
| Context facts | tests/test_context_facts_8.cpp | prerequisiteFacts new fields, suggestedNextAction priority matrix (6 states + priority beats), injected in-flight predicate live updates | PENDING BUILD |
| Regression parity | test_schema_form_builder_v2, test_data_manager_panel, test_selection_context, test_command_registry, test_workbench_host, test_processing_history_model, test_provenance_section, test_workbench_shutdown_policy, test_temporal_scene_model, test_inspector_host, test_command_palette | 3.0/5.0/7.0 contracts stay green | PENDING BUILD |

Bounds used everywhere: offscreen platform, no network, synthetic data only,
bounded spin-waits (≤ 2 s), 1-thread pools injected where scheduling matters.
