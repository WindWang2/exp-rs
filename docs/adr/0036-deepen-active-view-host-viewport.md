# ADR 0036: Deepen ActiveViewHost Canvas Viewport Seams

- **Status**: Accepted
- **Date**: 2026-08-02
- **Deciders**: exp-rs Core Architecture Team

## Context

`ActiveViewHost` previously exposed read-only viewport queries (`mapCanvasExtent()`, `mapCanvasScale()`, `mapCanvasCrsAuthId()`), but external callers were forced to dereference `activeViewHost->mapCanvas()` directly to set extents, re-center, zoom to full extent, or refresh the canvas.

## Decision

1. **High-Level Viewport Operations**: Add `setExtent(const QgsRectangle &extent)`, `setCenter(const QgsPointXY &center)`, `zoomToFullExtent()`, and `refreshCanvas()` directly to `ActiveViewHost`.
2. **Headless Degradation**: When running headlessly (`m_mapCanvas == nullptr`), all viewport methods degrade gracefully as safe no-ops.
3. **Hide Pointer Dereferences**: Encapsulate canvas state mutation inside `ActiveViewHost`, reducing direct `QgsMapCanvas` pointer coupling in UI and IPC caller modules.

## Consequences

- **Locality**: Viewport navigation and repainting concentrate inside `ActiveViewHost`.
- **Testability**: Viewport operations are testable headlessly without throwing null pointer exceptions.
