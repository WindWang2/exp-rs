# TEST MATRIX — Professional Workbench 8.0

Evidence legend: COMPILED / EXECUTED-FAIL / EXECUTED-PASS / NOT-RUN (env).
All runs: offscreen QPA (`QT_QPA_PLATFORM=offscreen`), Release `-O3`, Linux.

## New suites (this track)

| Suite | Result | Coverage |
|-------|--------|----------|
| test_schema_form_4 | EXECUTED-PASS — 11/11 cases, 80 assertions | nested objects (round-trip, nested required, optional-group absence, soft-range warnings, conditional visibility in groups, depth-cap → JSON editor), object arrays (minItems seeding, typed round-trips, maxItems add-gate, minItems remove-gate, positional validation paths, 306-item honest truncation), dynamic enum provider (resolve, refresh with values context, unknown-source free-text degradation, no-provider), async path_exists (hit/miss/supersede/teardown-drop), a11y names/descriptions |
| test_asset_preview_service | EXECUTED-PASS — 8/8 cases, 1104 assertions (14 consecutive green runs; one unexplained abort under 3-concurrent-build machine load, see PERFORMANCE notes) | raster gradient render + aspect fit, NoData → black, typed failures (missing/garbage), vector QGIS render + typed >cap refusal, cache hit provenance + LRU eviction (entries/bytes), supersede, cancel, dead-receiver drop |
| test_asset_catalog_index | EXECUTED-PASS — 8/8 cases, 327 assertions | index mirror/update/remove idempotence, filter name/source/id, one-pass grouping, panel filter box, cap+sentinel truthful totals + non-selectable, lazy collection children (55 > 50), eager small collections, 200k-record scale evidence |
| test_context_facts_8 | EXECUTED-PASS — 3/3 cases, 14 assertions | prerequisiteFacts 8.0 fields, suggestedNextAction priority matrix, injected in-flight predicate |

## Parity / regression suites (kept green)

| Suite | Result |
|-------|--------|
| test_schema_form_builder_v2 (SchemaForm 3.0 contract) | EXECUTED-PASS — 8 cases, 48 assertions |
| test_data_manager_panel | EXECUTED-PASS — 16 cases, 101 assertions |
| test_workbench_host (50) · test_selection_context (57) · test_command_registry (48) · test_command_palette (23) · test_processing_history_model (42) · test_inspector_host (27) · test_workbench_shutdown_policy (54) · test_provenance_section (61) · test_temporal_scene_model (16) | EXECUTED-PASS |
| test_theme_selector_parity (85) · test_adversarial_m4 (558) · test_adversarial_m5 (81) · test_adversarial_m6 (37) · test_interactive_session_contract (18) · test_active_view_host_viewport (12) · test_workflow_session_controller (17) · test_workflow_pipeline_ui (107) · test_ui_task_center_contract (174) | EXECUTED-PASS |
| sicnu_geo_rs app target | COMPILED + LINKED |

Bounds: no network, synthetic GDAL fixtures only, bounded spin waits
(≤ ~2 s), injected 1–2 thread pools where scheduling matters.
