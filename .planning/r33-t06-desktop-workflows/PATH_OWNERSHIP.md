# R33-T06 — PATH OWNERSHIP

Precedence as required by the brief: **active PR exclusive paths > this batch's
whole-file exceptions > this track's explicit write set > directory summaries.**

## Exclusive paths held by open PRs at start (do not write)

### #2134 (`f7b0ebb9c6ce`)
```
.planning/r32-t05-agent-runtime/**            .gitignore
docs/development/pi-product-agent/conversation-recovery.md
pi/product-runtime/protocol.mjs               pi/product-runtime/test/checkpoint-redaction.test.mjs
resources/translations/sicnu_zh_CN.ts         scripts/stage_windows_application.ps1
src/agent/agent_copilot_dock_widget.{cpp,h}   src/agent/raster_display_service.{cpp,h}
src/agent/runtime/agent_tool_host.{cpp,h}     src/agent/runtime/pi_process.{cpp,h}
src/agent/view_control_service.{cpp,h}        src/agent_loop/decision_record.cpp
src/agent_ops/operations_coordinator.cpp      src/plugins/host/CMakeLists.txt
src/sdk/exprs/plugin_{discovery,package,registry,snapshot}.cpp
tests/CMakeLists.txt                          tests/test_agent_copilot_ui.cpp
tests/test_agent_tool_host.cpp                tests/test_pi_process.cpp
tests/test_python_agent_gui_delivery.cpp      tests/test_r23_e_python_worker_typed.py
tests/test_r32_t05_agent_dock.cpp             tests/test_r32_t05_ops_sdk.cpp
```

### #2135 (`5150d2d1c451`)
`docs/processing/geometric-registration.md`; `src/analysis/classification/rs_classification_pipeline.*`,
`rs_training_data_extraction.*`; `src/analysis/segmentation/rs_roi_labeler.cpp`;
`src/operators/rs/rs_supervised_classification_operator.cpp`;
`src/processing/algorithms/{change_detection.h,feature_matcher.cpp,mosaic_seamline.*,primitives/raster_histogram.cpp,registration/*,spectral_roi.cpp}`;
`tests/{test_feature_matcher,test_mosaic_seamline,test_object_hierarchy,test_primitives5,test_spectral_roi,test_stack_registrator,test_training_data_extraction}.cpp`

### #2136 (`57f99a4fcde8`)
`src/geospatial/fabric/chunk_plan.*`, `src/geospatial/identity/asset_identity.cpp`,
`src/geospatial/io/subdataset_inventory.cpp`, `src/geospatial/remote/{http_fetch,range_cache}.cpp`,
`src/geospatial/util/resource_uri.cpp`, `src/geospatial/vector/vector_writer.*`,
`tests/{CMakeLists.txt,test_r32_t03_data_io.cpp}`

### #2137 (`99262a739a97`)
`src/operators/runtime/{detection_tile_engine.cpp,provenance_verify.cpp,tensor_blob.*,tile_inference_engine.*}`,
`tests/{CMakeLists.txt,test_r32_t02_*}`

## This track's write set (used so far)

| Path | Change | Owner |
|---|---|---|
| `src/app/preview/asset_preview_service.{h,cpp}` | WP02/WP03/WP04/WP05 | T06 |
| `src/app/panels/data_manager_panel.{h,cpp}` | WP02 consumer (re-read once) | T06 (declared in the track's write set) |
| `tests/test_r33_t06_preview_identity.cpp` | new | T06 |
| `tests/CMakeLists.txt` | one appended target block at EOF only | T06 |
| `scripts/audit_i18n_strings.py` | WP19 classifier precision | T06 (declared) |
| `docs/ui-architecture.md` | §22 semantics updated | T06 — **not** in the brief's literal write set; no open PR holds it (checked: none of #2134–#2137 touch it). Ledger gap, recorded in REVIEW_R2. |
| `.gitignore` | appended markdown-only exception for this track | T06 — the brief explicitly allows "`.gitignore` 只允许自身planning记录的必要追加，不改原PR块"; the block is EOF-only and touches no existing rule. #2134 also appends at EOF, so a textual conflict is possible there but is a pure-addition conflict. |
| `.planning/r33-t06-desktop-workflows/**` | this ledger | T06 |

## Deliberately NOT touched

* `resources/translations/sicnu_zh_CN.ts`, `scripts/stage_windows_application.ps1` — #2134.
* `tests/test_bilingual_delivery_ui.cpp` — T05 (T06 hands over QM assertions).
* `src/agent/cartography/cartography_operators.cpp`,
  `tests/test_cartography_{production_11,operators_10}.cpp` — T04.
* `src/data/execution_fingerprint.*`, `src/platform/portable.h`,
  `src/geospatial/util/atomic_fs.*` — T03/T04.
* `src/app/workbench/mission_*`, `src/app/agent_ops/**` — T05.
