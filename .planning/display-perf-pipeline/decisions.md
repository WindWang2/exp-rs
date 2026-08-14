# Decisions: Display Refresh Pipeline Optimization

## Architectural Decisions

### 1. Dual Viewport Sync Controller Refactoring
- **Safety**: Use `QPointer<QgsMapCanvas>` for `mPrimary` and `mSecondary`. If either canvas becomes null, cancel pending timer and abort execution cleanly without crashing.
- **Timer Throttle Semantics**:
  - Keep `16 ms` rate-limiting window (~60 FPS).
  - When `onPrimaryExtentChanged` or `onSecondaryExtentChanged` fires:
    - Update `mPending` to the new source ("latest-event-wins").
    - If `!mThrottle.isActive()`, start the timer (`mThrottle.start()`).
    - If `mThrottle.isActive()`, DO NOT restart the timer (let the existing timer expire to guarantee frame delivery every 16ms under sustained load).
- **Targeted Application (`applyFromPrimary` / `applyFromSecondary`)**:
  - Remove the source-canvas `refresh()` call (`mPrimary->refresh()` in `applyFromPrimary` and `mSecondary->refresh()` in `applyFromSecondary`).
  - Order of operations:
    1. Align rotation if different (`!qgsDoubleNear(target->rotation(), source->rotation(), 1e-5)`).
    2. Align extent if different (`target->extent() != source->extent()`).
    3. If `mScaleSync` is enabled and scale is not yet aligned (`!qgsDoubleNear(target->scale(), source->scale(), 1e-4)`), call `target->zoomScale(source->scale())`.
    4. Call `target->refresh()` only if changes were applied to target.
  - Reentrancy guard: Keep `mApplying = true` during sync application to prevent feedback loops.

### 2. QgisDisplayManager Layer Synchronization
- **Deferred vs Synchronous Bridge Sync**:
  - Introduce batch synchronization support: `QgisDisplayManager::batchUpdate(DisplayViewId, std::function<void()>)` or a lightweight `ScopedBatchUpdate` (or deferred synchronization flag).
  - When batching is active, defer `bridge->setCanvasLayers()` until the scope ends, performing exactly ONE canvas layer sync and ONE canvas refresh at the end of the batch.
  - For normal individual operations when not in a batch, preserve synchronous synchronization for backward-compatibility while eliminating redundant calls (e.g. avoid calling `setCanvasLayers()` twice per operation).
- **Correctness Guarantee**:
  - Calling `bridge->setCanvasLayers()` rebuilds the layer stack directly from the underlying `QgsLayerTree`, ensuring 100% fidelity with QGIS layer tree order and visibility.

### 3. Non-intrusive Test Seams & Instrumentation
- Provide lightweight statistics / probe accessors on `RsDualViewportSyncController` for testing:
  - `statsExtentChangedCount()`
  - `statsAppliedSyncCount()`
  - `statsTargetRefreshCount()`
  - `resetStats()`
- Provide lightweight statistics / probe on `QgisDisplayManager` or test seam:
  - `canvasLayerSyncCount(DisplayViewId viewId) const`
- These are pure in-memory counters with zero runtime overhead and zero UI noise in production.
