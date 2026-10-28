# R25-D TEST RESULTS (live log; final state at PR time)

Environment: build-ci-fast (Release, ENABLE_TESTS=ON), QT_QPA_PLATFORM=offscreen,
CTEST_PARALLEL_LEVEL=1, shared-host flock build (-j2). Host: Linux 6.18-cachyos, 16c/62G.

## This track's suites — GREEN ×2 (protocol: run twice)

`ctest -R "^test_r25_d_" -j1` — **11/11 passed, twice** (2026-10-28):

| Suite | Cases | Notes |
|---|---|---|
| test_r25_d_cartography_ux | 3 | pure ContextRules + wiring scans |
| test_r25_d_cartography_dock | 2 (5 sections) | REAL dock; designer hand-off via real LayoutService/project manager |
| test_r25_d_operator_preflight | 2 | real sidecars (59 entries) + wiring scan |
| test_r25_d_result_open_link | 1 (4 sections) | source pins of the four result-open connections |
| test_r25_d_canvas_invalidation | 1 (2 sections) | structural render-discipline counts |
| test_r25_d_e2e_display_cartography | 1 | full chain incl. real TaskCenter jobs + project roundtrip |

RED evidence: master-state failure modes were established statically and dynamically —
- F8 (compose source-path resolution): the E2E FAILED on first run at cartography:compose
  with `map frame 'map-1': layers: unresolvable reference(s): …product.tif, …ndvi.tif`
  (logged in LOOP_LEDGER #7) — that run WAS the master-behavior RED (compiler unchanged
  at that point); the fix turned it GREEN.
- Wiring scans fail on master sources by construction (no hasMapLayers / no setEstimate
  callers / loadRasterLayer bindings / no ribbon cartography projections).

## Neighborhood regression — all green (2026-10-28)

Direct binary runs (whole suites):
- test_mapspec (239 cases / 2830 assertions) — includes cartography operators/tokens/
  library/quality/templates/visual/platform5-9 — covers the mapspec_compiler change.
- test_qgis_display_manager (25 / 6490) — display plane incl. batch coalescing.
- test_command_registry (11 / 57), test_selection_context (15 / 72),
  test_command_palette (4 / 23), test_shortcut_conflicts (4 / 28).
- test_workbench_full_shell_lifecycle (7 / 126) — real QgisDesktopWindow offscreen:
  ribbon/menus/dock wiring changes hold through open/save/new/close lifecycle.

## CP11 render-path suite (W3/W7 evidence)

test_cartography_production_11 (render gate RUNS by default; #1655 fixed-in-master):
- **29/32 cases passed** on this host — the 5 formerly-gated render cases ran for real
  (no SICNU_CP11_RENDER opt-out); render/export path executes on this host.
- 3 failures under comparison against master-state compiler (in progress; verdict below).
- Timing: (to be recorded in PERF_BASELINE.md).

## Registration integrity

`python3 scripts/check_merge_registration_integrity.py --base origin/master --head HEAD --require-refs`
→ `1069 registered targets + 50 add_test names at HEAD (base: 1063/50; lost: 0+0; added: 6+0); 0 marker findings; 0 duplicates`
