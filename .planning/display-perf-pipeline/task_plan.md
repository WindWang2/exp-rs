# Task Plan: Display Refresh Pipeline Optimization (`exp-rs`)

## Goal
Perform a strict-scope, high-impact performance optimization of the desktop GIS 2D display refresh pipeline:
1. Dual viewport pan/zoom/rotation synchronization controller (`RsDualViewportSyncController`).
2. DisplayManager layer synchronization (`QgisDisplayManager`).
3. Minimize redundant `refresh()`, `render()`, and `setCanvasLayers()` calls during panning, zooming, rotation, and batch layer mutations.
4. Eliminate UI jitter/stutter while strictly preserving correctness, QGIS layer ordering, visibility semantics, and bidirectional synchronization.

## Phase 1: Deep Analysis & Architecture Invariants
- [x] Create worktree `../exp-rs-display-perf` on branch `perf/display-refresh-pipeline`.
- [x] Initial build and test run confirmation.
- [x] Profile / analyze the exact refresh paths in `RsDualViewportSyncController` and `QgisDisplayManager`.
- [x] Identify redundant render calls, reentrancy guards, timer behavior, lifetime management, and layer synchronization overhead.
- [ ] Document findings in `findings.md` and decisions in `decisions.md`.

## Phase 2: Instrumentation & Test Seams (Non-intrusive)
- [ ] Add test-only instrumentation / counters to `RsDualViewportSyncController`:
  - `extentsChanged` event counter
  - applied sync counter
  - canvas refresh request counter
- [ ] Add test-only instrumentation / scoped batching or query seams to `QgisDisplayManager`:
  - `setCanvasLayers` sync count
  - batch synchronization interface or deferred coalescing
- [ ] Ensure zero profiling noise in release UI builds.

## Phase 3: Dual Viewport Sync Optimization (`RsDualViewportSyncController`)
- [ ] Replace raw `QgsMapCanvas*` with `QPointer<QgsMapCanvas>` for safety during destruction.
- [ ] Fix timer coalescing / throttle logic:
  - If timer is active, retain single-shot interval without postponing expiry indefinitely (true ~60 FPS rate limiting coalesce instead of starve-debounce).
  - Coalesce high-frequency updates, latest-event-wins.
- [ ] Optimize `applyFromPrimary` & `applyFromSecondary`:
  - Stop calling `mPrimary->refresh()` when syncing from primary to secondary (primary is the source and already rendered).
  - Stop calling `mSecondary->refresh()` when syncing from secondary to primary.
  - Check before mutate: compare rotation, extent, and scale before making changes; if destination already matches, do nothing.
  - Set rotation first if changed, then extent/scale.
  - Guard against destroyed canvases, reentrancy (`mApplying`), and disabled state.

## Phase 4: DisplayManager Layer Sync Optimization (`QgisDisplayManager`)
- [ ] Audit all layer tree mutations: `addLayer`, `cloneLayer`, `adoptLayer`, `relocateLayer`, `removeLayer`, `moveLayerTop`, `moveLayerBottom`.
- [ ] Recognize that `QgsLayerTree` mutations (`insertLayer`, `addLayer`, `removeChildNode`, `move`) already emit `layerOrderChanged` which queues `QgsLayerTreeMapCanvasBridge::deferredSetCanvasLayers()`.
- [ ] Provide scoped batching or deferred coalescing to avoid O(N) duplicate refreshes during batch layer operations.
- [ ] Ensure layer ordering, visibility, and presentation semantics remain 100% identical.

## Phase 5: Targeted Deterministic Tests & Benchmarks
- [ ] Implement the 10 acceptance test scenarios in `test_dual_viewport_sync.cpp` and `test_qgis_display_manager.cpp`:
  1. primary -> secondary pan
  2. secondary -> primary pan
  3. rapid 100/500 extentChanged updates (measure before/after)
  4. scale sync on/off
  5. rotation sync
  6. enable/disable during pending event
  7. canvas destroyed during pending timer
  8. rapid alternating source viewport events
  9. layer add/remove/reorder
  10. multi-layer batch (measure setCanvasLayers count before/after)
- [ ] Verify ASan/UBSan build passes without memory leaks or use-after-free.
- [ ] Record deterministic before/after benchmarks in `benchmark.md`.

## Phase 6: Code Review & PR
- [ ] Run independent subagent code review with focus on:
  - redundant render
  - feedback loops
  - timer lifetime & stale `QPointer`
  - layer order / visibility semantics
  - reentrancy & race conditions
- [ ] Commit with message: `perf(display): coalesce viewport and layer refresh work`
- [ ] Push branch `perf/display-refresh-pipeline` to origin.
- [ ] Create PR to `master` with full before/after documentation.
