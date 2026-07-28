# Spec: View-Oriented Main Shell (elevate Data Manager)

**Parent:** ADR 0009, ADR 0010, `2026-07-24-data-manager-architecture-spec.md`, `2026-07-26-multiple-display-views-spec.md`  
**Status:** In progress — Waves A–D landed (shell multi-view chrome included).

## Problem

Engine seams for Data/Display exist and ProjectContext already hosts multi-view APIs. The **main shell** still teaches the wrong model:

- Left **Layers** tree looks like “all project data”.
- **Data Manager** is secondary (tabbed under Layers; not raised by product shell).
- **LayerManager** name implies data ownership; it is only a main-view load helper.
- Bypass paths still `addMapLayer` / open files without Asset registration (OBIA canvas, STAC, some dialogs).

**Goal:** Main UI is **view-oriented**: Data Manager is the elevated catalog; the layer tree is the active Display View’s presentation stack.

## Target shape

```text
QgisDesktopWindow (shell)
├─ Ribbon / chrome
├─ ProjectContext                          sole Data+Display host
│  ├─ DataManager                          catalog authority
│  └─ DisplayManager
│     ├─ Main Display View  ←── main canvas + “视图图层” tree
│     └─ Secondary views    ←── later: split / session hosts
├─ DataManagerPanel                        primary left catalog (data)
├─ View layer tree dock                    active view’s Display Layers only
└─ ActiveViewHost (ex-LayerManager)        path→register+addLayer(activeView)
```

### User-visible language

| UI (中文) | Domain term |
|-----------|-------------|
| 数据管理 | Data Manager / Data Asset catalog |
| 视图图层 | Display Layers of the **active** Display View |
| 添加到显示 | displayRequested → addLayer(activeView) |
| 从视图移除 | remove Display Layer (keep Asset) |
| 卸载数据 | DataManager unload plan |

### Shell invariants

1. Active view defaults to `mainViewId()` until a view switcher exists.
2. Open/import/sample → register Asset → optional display on active view.
3. Layer tree selection never implies Asset unload.
4. Session windows may use secondary views or private canvases; **commit to main map** goes through ProjectContext (register + addLayer main).

## Waves

### Wave A — Shell language + default focus (this wave)

- Rename Layers dock window title to **视图图层** (objectName stays `layersDock` for layout state).
- Product shell **raises Data Manager** over Layers by default; both remain tabified left.
- Document LayerManager as ActiveViewHost façade in header comments (no large rename yet — avoids mass churn).
- ADR 0010 + this spec + CONTEXT vocabulary.

**Done when:** cold start shows 数据管理 front; Layers tab labeled as view stack; docs committed.

### Wave B — Kill bypass paths (main map)

Single entry for “put this file/path on the main view as data”:

`LayerManager` / `loadDataLayer(path)` → `DataManager::registerSource` + `DisplayManager::addLayer(mainViewId)`.

Migrate call sites (priority order):

1. ~~`loadDataLayer` / `loadRasterLayer` / sample load~~ (LayerManager already Data+Display)
2. ~~Task Center / job panel “load result”~~ → `loadDataLayer` / loadVector
3. ~~Algorithm dialog success → main~~ (no dual `addMapLayer` fallback when mainWin present)
4. ~~STAC browser~~ → `loadDataLayer(/vsicurl/…)`
5. ~~OBIA session canvas~~ → private shared_ptr layers only; 加载到主图 → `loadDataLayer`
6. ~~Georef load to main~~ → walk parent to main window `loadRasterLayer`
7. Remaining: session-private `QgsMapLayerStore` (classify/georef dual-canvas) — Wave E

**Done when:** grep for `QgsProject::instance()->addMapLayer` in `src/app` only remains for true QGIS interop adoption or session-private stores that later call ProjectContext adopt.

### Wave C — ActiveViewHost rename + API ✅

- Renamed `LayerManager` → `ActiveViewHost` (`src/app/active_view_host.*`).
- Interface: `openPath` / `openRasterPath` / `openVectorPath`, `displayAsset`, `removeSelectedDisplayLayers`, `activeViewId` / `setActiveViewId` / `mainViewId`.
- Compatibility aliases: `loadLayer` / `loadRasterLayer` / `loadVectorLayer` / `removeSelectedLayers`.
- Data Manager panel “显示” routes through `displayAsset`.
- Tests: `test_active_view_host_data_context` (includes displayAsset + setActiveViewId).

### Wave D — Multi-view shell chrome ✅

Depends on ProjectContext multi-view host (createSecondaryView / removeView). Shell:

- Horizontal `QSplitter`: main canvas | `SecondaryMapViewWidget` (independent tree + store + canvas)
- 视图菜单：**第二视图** (Ctrl+Shift+2)、激活主/第二、同步主视图图层
- ActiveViewHost routes open/display to active view id
- Secondary header: 活动 / 同步主视图 / 关闭

Out of scope still: persist secondary views to .qgz; linked extents; more than two views.

### Wave E — Session windows as Display Views

**Done:**

- `Sicnu::session_map` shared lib: `RsSessionMapWorkspace` (`viewSpec` / `releaseLocalBridge` / `restoreLocalBridge`).
- Classification + OBIA: session map registered as secondary Display View on open; load-to-main via `requestLoadToMainMap` → `loadDataLayer`.
- Georef I2I: SRC + REF each get a session workspace and secondary view; I2M: SRC session view only.
- Georef warp output: `requestLoadToMainMap` → `loadDataLayer` (no silent dual-path when host is connected).
- Tests: session bind without dual bridge; dual session maps (I2I shape) in `test_project_context_reap`.

**Remaining (optional follow-ups):** session layers as Data Assets via `DisplayManager::addLayer(assetId)` rather than raw `QgsMapLayer*`; persist session view ids.

## Testing

- Wave A: manual + existing panel persistence (objectNames stable).
- Wave B: unit tests on openAndDisplay; optional grep-gate in CI later.
- Wave C: `test_layer_manager_data_context` extended or renamed.
- Wave D: offscreen dual-canvas tests already planned in multi-view spec.

## Out of scope

- Rewriting DataManager internals.
- Persisting secondary views to .qgz (still deferred).
- Replacing QGIS layer tree widgets with a custom tree (reuse QgsLayerTreeView for main view).

## First code changes (Wave A)

| File | Change |
|------|--------|
| `main_window_docks.cpp` | Layers title 视图图层; product shell raise Data Manager |
| `layer_manager.h` | Comment: ActiveViewHost façade, not data authority |
| `CONTEXT.md` | Active Display View / View layer tree |
| ADR 0010 + this spec | Architecture record |
