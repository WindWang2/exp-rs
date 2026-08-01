# 0026 ActiveViewHost Context Menu Seam Architecture

We decided to deepen `ActiveViewHost` to absorb canvas zoom operations (`zoomToLayer`, `zoomToNativeResolution`) and refactor `LayerTreeMenuProvider` to consume `ActiveViewHost` as its sole shell facade seam.

### Context & Decision
Previously, `LayerTreeMenuProvider` in `src/app/layer_tree_menu.h` took raw pointers to `QgsLayerTreeView *view`, `QgsMapCanvas *canvas`, and `QgisDesktopWindow *window`. Context menu items contained inline lambdas calculating 1:1 native resolution extents and directly mutating `mCanvas`. This violated ADR 0015's single-seam principle by leaking raw canvas geometry and main window slots into UI context menus.

1. **Canvas Zoom Encapsulation**: Deepen `ActiveViewHost` with `zoomToLayer(QgsMapLayer *layer = nullptr)` and `zoomToNativeResolution(QgsMapLayer *layer = nullptr)`. Raster units per pixel math and canvas extent refreshes are encapsulated inside `ActiveViewHost`.
2. **Single Seam Context Menu Provider**: `LayerTreeMenuProvider` is constructed with `(QgsLayerTreeView *view, ActiveViewHost *activeViewHost)`. It holds zero pointers to `QgisDesktopWindow` or `QgsMapCanvas`.
3. **Unified Locality & Leverage**: All layer presentation, properties dialogs, zoom actions, and dataset opening commands route through `ActiveViewHost`.
