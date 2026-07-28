# 0010 View-Oriented Main Shell (Data Manager elevated)

## Status

Accepted — Waves A–E shell migration complete (session windows as secondary Display Views; asset-backed session layers optional follow-up).

## Context

ADR 0009 split **Data Assets** (Data Manager) from **Display Layers** (Display Manager). Engine seams exist:

- `sicnu::data::DataManager` — catalog, leases, revisions, unload plans
- `sicnu::display::QgisDisplayManager` — Display Views / Layers, multi-view APIs
- `sicnu::app::ProjectContext` — project host (main + secondary views, clear/close)

The **main shell** still presents a mixed model:

1. **Layers dock** (QGIS layer tree) looks like the data authority.
2. **Data Manager panel** is a second catalog of the same project data.
3. **`LayerManager`** is a compatibility façade over Data+Display for the main view only.
4. Many call sites still **`QgsProject::addMapLayer` / load path strings** (OBIA canvas, STAC, some algorithm dialogs, session workspaces), bypassing Asset identity.

Analysts and agents cannot reliably answer: “what data is in the project?” vs “what is shown in this view?”

## Decision

Elevate the Data Manager as the **only project data catalog** in the shell, and make the main interface **view-oriented**:

| Surface | Role |
|---------|------|
| **Data Manager panel** | Project data identity — assets, collections, status, unload/promote |
| **View layer tree** (renamed from “Layers”) | Presentation stack of the **active Display View** only |
| **Main map canvas** | The **Main Display View** (QGIS-interop view; `mainViewId()`) |
| **ProjectContext** | Sole host for Data Manager + Display Manager; active-view routing |

### Shell rules

1. **Import / open / sample load** → `DataManager::registerSource` (or restore) then optionally `DisplayManager::addLayer(activeViewId, assetId)`.
2. **Remove from view** ≠ unload asset. Layer tree remove = remove Display Layer (release lease). Unload only via Data Manager (with plan/cascade).
3. **Show on map** from Data Manager = `displayRequested` → `addLayer(activeViewId, …)` (already the panel signal).
4. **Processing inputs** prefer AssetId (existing #36–#40 track); shell does not pass “current QgsMapLayer” as identity.
5. **Secondary Display Views** (compare / session windows) attach via `createSecondaryView`; they do not create parallel QgsProject catalogs.

### Module deepening

| Module | Interface (small) | Implementation (deep) |
|--------|-------------------|------------------------|
| **DataManager** | register / query / lease / unload / promote | providers, SourceKey, leases, DAG, reaping |
| **DisplayManager** | createView / addLayer / removeLayer / listViews | Qgs stores, renderers, leases per view |
| **ProjectContext** | dataManager(), displayManager(), mainViewId(), views(), clear/close | adoption, multi-view set, session reaps |
| **ActiveViewHost** (new name for shell façade; today’s LayerManager) | loadPath → Asset+Layer on **active** view; remove selected **display** layers; refresh canvases | dialogs, tree selection, overview sync |

`LayerManager` is **not** a data authority. It becomes (or is replaced by) **ActiveViewHost**: display operations for the active view only. Deletion test: if callers must know Asset vs Layer, the host interface is wrong.

### What we reject

- Using the QGIS layer tree as the project data catalog.
- New “manager” classes that wrap QgsProject without AssetId.
- Building a second full project store for each session window without DisplayManager.
- Hypothetical multi-view chrome before ActiveViewHost + bypass kill list (engine multi-view already exists; shell follows).

## Consequences

**Positive**

- One mental model: Data Manager = what I have; View tree = what I see here.
- Unload / temporary / multi-view / processing AssetRef all attach to the same deep modules.
- Session windows (classify, OBIA, georef) can migrate to secondary views without new identity models.

**Negative / cost**

- Dual left docks during migration (Data Manager + View layers) until users learn the split.
- Bypass call sites must be fixed incrementally (OBIA result load, STAC, algorithm dialogs).
- Naming: English “Layers” → “View layers” / 中文「视图图层」 to avoid implying data ownership.

## Migration order (waves)

See `docs/superpowers/specs/2026-07-27-view-oriented-main-shell-spec.md`.

## Related

- ADR 0009 — Data Assets vs Display Layers  
- Spec 2026-07-24 data-manager architecture  
- Spec 2026-07-26 multiple display views (engine + ProjectContext host)  
- Issues: #68 multi-view host, #69 addLayer-twice invariant, #36–#40 AssetRef processing  
