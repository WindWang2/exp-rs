# R27-T04 File Ownership

## Production files I will modify (all inside whitelist)
- `src/app/main_window_processing.cpp` (#1745)
- `src/app/map_tools/rs_roi_spectrum_tool.cpp` / `.h` (#1745 if needed for lifecycle seam)
- `src/app/classification/qgsclassificationmainwindow.cpp` / `.h` (#1746, #1792, #1794)
- `src/app/data_project_serializer.cpp` / `.h` (#1747 preflight API)
- `src/app/main_window_project.cpp` (#1747 preflight call)
- `src/app/dialogs/stac_browser_dialog.cpp` / `.h` (#1788, #1789 UI, #1765 call-site)
- `src/app/stac_client.cpp` / `.h` (#1789 signal semantics) — NOTE: this is `src/app/stac_client.*` (Qt browser client), NOT `src/geospatial/stac/*`
- `src/app/widgets/cross_section_widget.cpp` / `.h` (#1790)
- `src/app/classification/rs_roi_tool_magicwand.cpp` / `.h` (#1791)
- `src/app/workflow/pipeline_editor_dock.cpp` (#1793)
- `src/app/workbench/mission_context_store.cpp` / `.h` (#1795)
- `src/app/main.cpp` (#1765 policy engage)
- P3 (#1805) capacity-gated: `src/app/widgets/temporal_workbench_panel.cpp` (F17), `src/native/.../qgsmacnative.mm` (F6), `src/app/rs_obia_main_window.cpp` (F13)

## Test files (new, unique prefix)
- `tests/test_r27_t04_roi_spectrum_reinvoke.cpp`
- `tests/test_r27_t04_classification_orphan.cpp`
- `tests/test_r27_t04_project_downgrade.cpp`
- `tests/test_r27_t04_stac_pagination.cpp`
- `tests/test_r27_t04_cross_section_async.cpp`
- `tests/test_r27_t04_magicwand_async.cpp`
- `tests/test_r27_t04_recompute_async.cpp`
- `tests/test_r27_t04_export_atomic.cpp`
- `tests/test_r27_t04_pipeline_json.cpp`
- `tests/test_r27_t04_mission_sidecar.cpp`
- `tests/test_r27_t04_network_policy_gui.cpp`
- (P3) `tests/test_r27_t04_p3_sweep.cpp`

## Shared (protected, append-only)
- `tests/CMakeLists.txt` — single append block marked `# ── R27-T04 ...` at end.

## Never touched
- `src/runtime/**`, `src/geospatial/**` (incl. canonical `src/geospatial/stac/stac_client.*`), `src/agent_ops/**`, `src/app/agent_ops/**`, `docs/adr/**`, root `CMakeLists.txt`, `.gitignore`.
