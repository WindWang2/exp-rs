# Spec: Multiple Display Views — engine + ProjectContext multi-view

**Parent:** `docs/superpowers/specs/2026-07-24-data-manager-architecture-spec.md` — Display Manager (lines 280-304: "A Display View represents a viewport and owns an ordered layer tree"; the `createView`/`addLayer`/`cloneLayer`/`removeLayer` API), Project Persistence (lines 424-445: main view + "extra Display Views" in the SICNU extension block), and the Deferred list line 530 ("multiple simultaneous Display Views").
**Status:** Proposed.

## Problem Statement

The motivating use case from the parent spec (line 9) — "one dataset displayed in multiple views with **independent band compositions and renderers**" (e.g. true-color in one view, false-color in another) — is already **fully supported by the `QgisDisplayManager` engine**. `createView`, `addLayer` to any view by id, `cloneLayer(source, target)` across views, per-view `QgsMapLayerStore`, per-display-layer Asset Leases (`leaseCount == 2` after a cross-view clone), and per-view renderer isolation are all implemented and tested (`test_qgis_display_manager.cpp`).

What is missing is everything **around** the engine that makes a second view a real, manageable thing:

1. **The display manager has no view lifecycle beyond create.** There is no `removeView`, no `listViews`/iteration, and no view-level signals (`viewAdded`, `viewAboutToBeRemoved`). A view is created and then lives until its QPointer canvas/store dies or the manager is destroyed — no explicit teardown, no enumeration, no observer notification. The shell cannot manage a set of views it cannot list or remove.
2. **`ProjectContext` hard-codes one view.** It holds a single `m_mainViewId`, `create()` calls `createView` exactly once, `mainViewId()` is the only accessor, `adoptExternalLayer` always adopts into the main view, and **`clearProject` only clears the main view** — a second view's layers and leases leak until the destructor reaps them. There is no path to create, track, or tear down a second view through the catalog host.
3. **The independent-`addLayer`-twice path is untested.** The existing cross-view test exercises `cloneLayer`; the equally-important "add the same asset to two views via two independent `addLayer` calls" (independent presentation, two leases) is not asserted, though the code path is near-identical. A regression here would silently break the core multi-view invariant.

The shell UI (a dock/split/window hosting a second `QgsMapCanvas`) and the persistence of "extra Display Views" (parent spec line 440) are **out of scope** for this wave — they are deferred until a second real caller exists (e.g. the classification-window migration behind the Display Manager, parent spec line 304), per the standing constraint against building for hypothetical callers.

## Solution

Close the engine-and-host gap so a second view is a first-class, manageable, tear-down-able object — without touching the shell or persistence.

- **Display manager: complete the view lifecycle.** Add `removeView(DisplayViewId)` (removes every display layer in the view — releasing their leases — then drops the view record), `listViews()` (returns the live `DisplayViewId`s in stable creation order), and three signals — `viewAdded(DisplayViewId)` (fired by `createView` once the record is stored), `viewAboutToBeRemoved(DisplayViewId)` (fired before the view's layers are dropped, while the canvas/tree/store are still valid so observers can detach widgets), and `viewRemoved(DisplayViewId)` (fired after the record is erased, for post-teardown bookkeeping). This mirrors the DataManager's `assetAboutToUnload`/`assetRemoved` two-phase removal precedent. These are additive, surface-level methods on the established seam; the core multi-view data model (per-view store/tree/bridge) is unchanged.
- **ProjectContext: become a multi-view host.** Replace the single `m_mainViewId` with a tracked set (the main view retains its special role for QGIS-interop adoption, but it is one entry in the set). Add `createSecondaryView(DisplayViewSpec)` returning a `DisplayViewId` (delegates to `m_displayManager.createView`, records the id), `views()` (the live set including main), and a `removeView(DisplayViewId)` pass-through (refuses the main view — it is QGIS-native and owned by the project). `clearProject` and `closeSession` now iterate **all** views (remove every display layer in every view before unloading assets), closing the leak. `adoptExternalLayer` continues to target the main view (unchanged — legacy adoption is a main-view concern).
- **Main view is special but not exclusive.** The main view (created by `ProjectContext::create`) remains the QGIS-interop view (its layer tree is the project's `layerTreeRoot()`, readable by ordinary QGIS). Secondary views are engine-only (their trees are independent `QgsLayerTree`s supplied by the host). This matches the parent spec's main-view-vs-extension-view split (lines 424-445) without yet persisting the extension views.

## User Stories

1. As an analyst, I want to open the same raster in a second view with a different renderer, so that I can compare true-color and false-color side by side.
2. As an analyst, I want to remove a view I no longer need, so that its layers and leases are released (the asset stays loaded if another view still shows it).
3. As a developer (host), I want to enumerate the live views, so that I can build a view-switcher or per-view UI.
4. As a developer (host), I want a signal when a view is added or about to be removed, so that I can attach/detach shell widgets without polling.
5. As a developer, I want `clearProject` to tear down ALL views, not just main, so that closing a project never leaks layers or leases from a secondary view.
6. As a developer, I want the main view to be non-removable through the secondary-view path, so that the QGIS-interop view cannot be accidentally destroyed out from under the project.
7. As a developer, I want adding one asset to two views (two `addLayer` calls) to produce two independent display layers with two leases and independent renderers — exactly like `cloneLayer` — so that the core multi-view invariant holds regardless of how the layers were created.
8. As a developer, I want the display-manager engine to stay the source of truth for views (ProjectContext just hosts a set of ids), so that the engine remains testable in isolation and the host stays thin.

## Implementation Decisions

- **`removeView` releases every display layer in the view first.** It iterates the view's `layerIds`, calling the existing `removeLayer` path per layer (which releases each lease and emits `layerRemoved`), then erases the `ViewRecord` and emits `viewAboutToBeRemoved` before the erase and `viewRemoved` after. Order: announce impending removal → drop layers → drop record. A view whose layers are all removed still exists until `removeView` (or manager destruction); `removeLayer` never removes a view.
- **`listViews` returns the live ids in stable creation order.** The current `std::map<QString, unique_ptr<ViewRecord>>` is keyed by the id's string form (UUID), so it iterates in UUID-string order — NOT creation order. To return creation order, the impl keeps a parallel `QVector<DisplayViewId>` of live ids (pushed on create, erased on remove) alongside the map, and `listViews` returns a copy of it. No snapshot staleness guarantee beyond "live at call time" (the host re-queries if it needs freshness).
- **`viewAdded` / `viewAboutToBeRemoved` are display-manager signals.** `createView` emits `viewAdded` at the end (after the record is stored). `removeView` emits `viewAboutToBeRemoved` before dropping layers (so observers can detach while the canvas/tree are still valid) and `viewRemoved` after. These are `Q_SIGNALS`; the host connects with the standard Qt mechanism.
- **ProjectContext holds a `QHash<DisplayViewId, ViewKind>` (or equivalent) tracking main + secondaries.** `ViewKind::{Main, Secondary}` lets `removeView` refuse the main view and lets `views()` report role if needed. The main view is added to the set in `create()`. `createSecondaryView(spec)` adds a Secondary entry. `clearProject`/`closeSession` iterate all entries.
- **`createSecondaryView` delegates to the display manager and records the id.** It does NOT auto-add any layers — the host decides what to show. The host supplies the `{canvas, layerTree, layerStore}` (this wave does not build a canvas; a follow-up wave or the classification migration supplies one).
- **`removeView` on ProjectContext refuses the main view** with a diagnostic (`project.main_view_not_removable` or similar); it otherwise forwards to the display manager and drops the entry from the set.
- **`clearProject` / `closeSession` iterate all views.** The current main-view-only loop becomes "for each view in the set, remove every display layer." This closes the leak where a secondary view's layers/leases survived `clearProject` until the destructor. Asset unload follows layer removal as today.
- **No persistence this wave.** Secondary views are session-only. On project reopen, only the main view exists (QGIS-native). Persisting "extra Display Views" (parent spec line 440) is a follow-up wave that needs a real caller and decisions about canvas/extent state serialization.
- **No shell UI this wave.** A dock/split/window hosting a second `QgsMapCanvas` is a follow-up. This wave's tests construct secondary views with locally-built `QgsMapCanvas`/`QgsLayerTree`/`QgsMapLayerStore` (the existing test harness pattern), proving the engine+host path without a shell.

### Decision-rich shape (from the design, not a working demo)

```text
// QgisDisplayManager additions (additive; createView/addLayer/cloneLayer/removeLayer unchanged)
QVector<DisplayViewId> listViews() const;
data::Result<void> removeView( DisplayViewId id );

Q_SIGNALS:
  void viewAdded( DisplayViewId id );
  void viewAboutToBeRemoved( DisplayViewId id );  // canvas/tree still valid
  void viewRemoved( DisplayViewId id );           // record gone

// ProjectContext: single m_mainViewId -> a tracked set
enum class ViewKind { Main, Secondary };
data::Result<DisplayViewId> createSecondaryView( const DisplayViewSpec &spec );
QVector<DisplayViewId> views() const;             // main + secondaries
data::Result<void> removeView( DisplayViewId id );  // refuses Main
// mainViewId() stays (the QGIS-interop view); views() includes it.
```

## Testing Decisions

- **The seam is the display manager + ProjectContext, offscreen.** Tests build `QgsMapCanvas`/`QgsLayerTree`/`QgsMapLayerStore` locally (the existing `test_qgis_display_manager.cpp` / `test_data_project_roundtrip.cpp` harness pattern) — no shell, no live rendering.
- **Display-manager cases:** `createView` emits `viewAdded`; `listViews` reports all live views in stable creation order; `removeView` drops the view's layers (releasing leases), emits `viewAboutToBeRemoved` (canvas still valid) then `viewRemoved` (record gone), and the view is gone from `listViews`; a view with a layer shown in another view does not unload the asset when removed (the other lease holds).
- **ProjectContext cases:** `createSecondaryView` returns a distinct id tracked in `views()`; `removeView` refuses the main view; `clearProject` removes layers across ALL views (no leak — assert a secondary view's layer and its lease are gone after clear, even though only main was cleared before); `views()` reports main + secondaries.
- **The independent-`addLayer`-twice invariant** (gap from the foundation map): add one asset to two views via two `addLayer` calls; assert two independent `QgsMapLayer`s in two stores, `leaseCount == 2`, and per-view renderer isolation (set opacity in one, the other is unaffected) — mirroring the existing `cloneLayer` test but via the add-twice path.
- **Prior art:** `test_qgis_display_manager.cpp:225-269` (cross-view clone + renderer isolation + lease==2), `test_data_project_roundtrip.cpp` (ProjectContext lifecycle).

## Out of Scope

- **Shell UI** for a second managed canvas (dock/split/window). Deferred until a real caller; this wave proves the engine+host path with test-built views.
- **Persistence of extra Display Views** (parent spec line 440). Deferred; needs a real caller and canvas/extent serialization decisions.
- **Synchronized extents / scale / CRS linking** between views. The current per-view `QgsLayerTreeMapCanvasBridge` implies independent extents; view-linking is a later enhancement.
- **Migration of `RsSessionMapWorkspace`** (classification window) behind the Display Manager (parent spec line 304). That is a real second caller and a separate wave.
- **A "new view" creation dialog / naming / active-view concept.** No shell this wave, so no view-management UI.
- **3D / non-2D views.** The current `QgsMapCanvas` model is 2D; a 3D view is a different adapter, out of scope.

## Further Notes

- This wave closes the engine-and-host gap the parent spec left open (line 530 deferred "multiple simultaneous Display Views" specifically because the *types and seams* were built to leave room — line 535 — without the host/persistence side). The engine was already built to spec; this wave makes the host multi-view-aware and completes the view lifecycle API.
- The main-view-is-special rule preserves QGIS interop (parent spec lines 424-433): the main view's layer tree is the project's `layerTreeRoot()`, so ordinary QGIS reads it; secondary views' trees are independent and engine-only until a persistence wave decides how (or whether) to serialize them.
- The leak in `clearProject` (secondary views' layers/leases surviving until the destructor) is a real defect this wave fixes as a side effect of making ProjectContext multi-view-aware — not a separate ticket, because the fix is the natural consequence of iterating the view set.
- Wave ordering within this spec: (1) display-manager `listViews` + `removeView` + view signals, (2) ProjectContext multi-view host (`createSecondaryView`/`views()`/`removeView`/clear-all-views), (3) the independent-addLayer-twice test locking the core invariant. Each is a small commit; (1) is testable in isolation before (2) hosts it.
