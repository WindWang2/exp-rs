# Findings: Display Refresh Pipeline Analysis

## 1. Dual Viewport Synchronization (`RsDualViewportSyncController`)

### Current State Analysis:
1. **Dangling Pointers**:
   - `mPrimary` and `mSecondary` are held as raw `QgsMapCanvas*` pointers.
   - If either canvas is deleted while `mThrottle` timer is scheduled, when `timeout` fires it accesses `mPrimary->...` or `mSecondary->...`, resulting in use-after-free or segfault.

2. **Throttle / Coalesce Behavior**:
   - `schedule(fromPrimary)` currently calls `mThrottle.start()` unconditionally on every `extentsChanged` signal.
   - In Qt, `QTimer::start()` on an already active single-shot timer restarts the countdown. This turns the 16ms throttle into a debounce that starves execution during continuous high-frequency updates (e.g. dragging at 120Hz or rapid programmatic updates).
   - Expected behavior: If `mThrottle.isActive()`, keep the existing timer running so that an update fires every ~16ms (~60 FPS interactive rate-limit), updating `mPending` to the latest source ("latest-event-wins"). Only if `!mThrottle.isActive()`, start the 16ms timer.

3. **Excessive & Redundant `refresh()` Calls**:
   - In `applyFromPrimary()`:
     ```cpp
     mSecondary->setExtent( mPrimary->extent() );
     if ( mScaleSync )
     {
         mSecondary->zoomScale( mPrimary->scale() );
         mSecondary->setRotation( mPrimary->rotation() );
     }
     mSecondary->refresh();
     mPrimary->refresh(); // <-- REDUNDANT!
     ```
   - Primary was the source canvas that just changed and triggered the signal. Primary canvas already scheduled/completed its own render. Calling `mPrimary->refresh()` inside `applyFromPrimary` forces primary to render twice per user interaction tick!
   - Calling `zoomScale` after `setExtent`:
     `QgsMapCanvas::zoomScale` calculates `zoomByFactor(newScale / scale())` which internally scales the extent, calls `setExtent(...)` AGAIN and calls `refresh()` inside `zoomByFactor`!
     When `scale()` already matches `mPrimary->scale()` within epsilon, `zoomScale` is completely redundant and causes extra calculations and an extra `refresh()`.
   - Calling `setRotation` after `setExtent`:
     Changing rotation in QgsMapCanvas alters the bounding box extent. Rotation should be aligned first if it differs, followed by extent and scale if needed.
   - If secondary already has the exact same extent, rotation, and scale as primary (e.g. no-op change or duplicate events), NO mutations and NO `refresh()` should be invoked at all.

4. **Reentrancy Protection**:
   - `mApplying` guard correctly prevents secondary's `extentsChanged` from triggering a loop back into primary while `applyFromPrimary` is running.
   - However, if the controller is disabled (`setEnabled(false)`), any active timer and pending state must be immediately stopped and cleared.

---

## 2. DisplayManager Layer Synchronization (`QgisDisplayManager`)

### Current State Analysis:
1. **Bridge and LayerTree Mechanics**:
   - Each view creates a `QgsLayerTreeMapCanvasBridge(spec.layerTree, spec.canvas, this)`.
   - `QgsLayerTreeMapCanvasBridge` connects to `QgsLayerTree::layerOrderChanged` via `deferredSetCanvasLayers()`, which uses `QMetaObject::invokeMethod(this, "setCanvasLayers", Qt::QueuedConnection)` to coalesce layer changes into a single canvas update in the Qt event loop.
   - In `QgsMapCanvas::setLayersPrivate(const QList<QgsMapLayer *> &layers)`, whenever the layer list changes:
     `refresh()` is called automatically.
   - However, in `QgisDisplayManager`:
     - `addLayer`: calls `layerTree->insertLayer(...)` (which emits `layerOrderChanged` -> queues `deferredSetCanvasLayers()`) AND THEN explicitly calls `bridge->setCanvasLayers()` synchronously.
     - `cloneLayer`: calls `layerTree->insertLayer(...)` AND THEN explicitly calls `bridge->setCanvasLayers()` synchronously.
     - `adoptLayer`: calls `bridge->setCanvasLayers()` synchronously.
     - `relocateLayer`: modifies `layerTree` AND THEN calls `bridge->setCanvasLayers()` synchronously.
     - `removeLayer`: modifies `layerTree` AND THEN calls `bridge->setCanvasLayers()` synchronously.
     - `moveLayerTop`: modifies `layerTree` AND THEN calls `bridge->setCanvasLayers()` synchronously.
     - `moveLayerBottom`: modifies `layerTree` AND THEN calls `bridge->setCanvasLayers()` synchronously.

2. **O(N) Render Storm during Batch Operations**:
   - When loading a project, adding multiple layers, cloning a set of layers, or removing a group of layers, each individual operation synchronously forces `bridge->setCanvasLayers()`.
   - Each call to `setCanvasLayers()` calls `mCanvas->setLayers(...)` which triggers a full canvas `refresh()` and re-render.
   - For N layers added in a batch, N full synchronous canvas refreshes occur, plus an extra Nth+1 queued refresh from the bridge's deferred event.
   - By supporting deferred / batched canvas synchronization, batch operations coalesce to a single `setCanvasLayers()` and a single `refresh()` after all mutations are done.

3. **Ordering & Visibility Invariants**:
   - `QgsLayerTreeMapCanvasBridge` calculates canvas layers in top-down layer-tree order, respecting visibility (`nodeLayer->isVisible()`) and custom layer order if configured.
   - Any batching or deferred synchronization MUST preserve the exact same final `QList<QgsMapLayer*>` order and visibility filtering as immediate synchronization.
