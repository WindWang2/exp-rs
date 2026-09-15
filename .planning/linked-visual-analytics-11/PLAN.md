# PLAN — linked-visual-analytics-11

Mission: 地图/图表/多视图选择、光标、范围与图层可见性联动 — assembled from
the EXISTING 10.0 seams (ViewLinkController, VaDataSource/VaChartWidget,
QgisDisplayManager, command registry), closing the four audited gaps:
shell never mounts ViewLinkController; no cursor link; no layer
visibility/style link; no process-level VA selection hub.

## Vertical architecture (all new business code in primary scope)

```
                 VaSelectionHub (process-level authority, WP-A)
      publish(VaSelectionEvent) → generation-stamped broadcast, echo suppression
     ▲        ▲         ▲              │
     │        │         │              ▼
  charts   cursor    brush        subscribers (panel, controllers)
     └────────┴──── VaWorkbenchPanel (WP-E) ────┘

  ViewLinkController (WP-B/C): extent groups + history/undo + cursor sync
  VaLayerLinkController (WP-D): visibility/opacity by AssetId across views
  shell wiring (WP-F/G): main_window_view/workbench + view.* commands
```

Identity rules (WP-A/D): view = DisplayViewId token; layer = DisplayLayerId
per view + AssetId as the CROSS-VIEW link key (never layer names); feature =
QgsFeatureId within a layer; point = QgsPointXY in a named CRS (WKT authority:
the emitting canvas's destinationCrs); pixel = canvas device px; region =
bounded rect in a named CRS. Chart subjects carry payload index only.

## Work packages → phases

| WP | Phase | Delivery |
|---|---|---|
| A | 1 | `src/app/visualanalytics/va_selection_hub.{h,cpp}`: typed VaSelectionEvent (subject kind: ViewPoint/ViewRegion/Layer/Feature/Pixel/ChartPoint/ChartCategory/ChartRange), origin token, hub-assigned monotonic generation, echo/loop suppression (re-entrant publish with same origin+generation during dispatch is dropped), bounded 64-entry history + stats. |
| B | 2 | ViewLinkController: named link groups (propagation scoped to group), bounded per-view viewport history + `restorePreviousViewport` (undo), keep throttle/reentrancy/detach; snap-on-link per group. |
| C | 2 | Cursor link in ViewLinkController: xyCoordinates → cross-CRS projection → peers (vertex-marker crosshair, cleared on Leave via event filter); `cursorProjected` signal for charts; hover sampling async via RsScanPool generation tokens with stale-drop (`va_cursor_probe`). |
| D | 3 | `src/app/visualanalytics/va_layer_link_controller.{h,cpp}`: visibility/opacity sync keyed by AssetId across views; observes one new minimal display-manager seam (`layerStateChanged` emitted from setLayerVisible + add/remove paths); fail-closed on missing asset identity (no name guessing). |
| E | 3 | Panel brushing via hub: chart range/category/point publish + consume (hist↔scatter stays client-side bounded); scatter pick → map marker on active view; map hover → panel readout (probe). Truncation flags stay truthful. |
| F | 4 | `view.*` commands (view.linkCenter/linkScale/linkCursor/linkVisibility/linkUndo/linkGroupsStatus…) registered append-only in command_defs.cpp with availability facts; append entries to data/help/commands.json (contract test enforces coverage). |
| G | 5 | Lifecycle hardening: QPointer guards, viewAboutToBeRemoved detach, project-cleared reset, layer-removal purge, hub dispatch-depth unwinding on receiver destroy, offscreen fast-destroy tests. |
| H | 6 | Tests: extend test_view_link.cpp (groups, history/undo, cursor cross-CRS known answer, loop suppression, fast destroy), test_visual_analytics.cpp (hub echo/generation/bounds, 100k logical events, brushing contract), command availability covered via TEST_MATRIX + contract test run. |

## Build / test commands (local evidence only)

- Configure (once, worktree-local):
  `cmake -S . -B build-dev -G Ninja -DCMAKE_BUILD_TYPE=Debug -DENABLE_TESTS=ON
   -DENABLE_LOCAL_BUILD_SHORTCUTS=ON -DCMAKE_PREFIX_PATH="C:\deps\Qt\6.8.0\msvc2022_64;C:\deps\qca-install;C:\deps\kc-install"
   -DCMAKE_TOOLCHAIN_FILE=C:\deps\vcpkg\scripts\buildsystems\vcpkg.cmake`
  (from configure_build.cmd; root build.cmd points at another worktree — unused)
- Build: `cmake --build build-dev -j2` (CMAKE_BUILD_PARALLEL_LEVEL=2; drop to
  -j1 under memory pressure).
- Targeted tests (QT_QPA_PLATFORM=offscreen, -j1):
  `ctest --test-dir build-dev -R "test_view_link|test_visual_analytics|test_dual_viewport_sync|test_command_contract_9" --output-on-failure -j1 -C Debug`

## Commits (atomic, one per phase minimum)

1. chore(planning): track seed + gitignore whitelist
2. feat(va): selection hub authority (WP-A)
3. feat(view): link groups, history/undo (WP-B)
4. feat(view): cursor link + async probe (WP-C)
5. feat(va): layer visibility/opacity link (WP-D) + display seam
6. feat(va): hub-mediated brushing (WP-E)
7. feat(shell): view.* commands + wiring (WP-F)
8. test+fix: lifecycle hardening (WP-G/H)
9. docs: workbench VA linking docs + ui-architecture sync
10. review remediation + final double-validation
