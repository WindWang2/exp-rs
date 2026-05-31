# Layer Properties in Right-Click Context Menu

**Date**: 2026-05-31

## Problem

The layer tree right-click context menu does not include a "Properties..." option. Users can only access layer properties via:
- Double-clicking a layer
- Menu bar "Layer > Layer Properties..." (Ctrl+I)

Standard QGIS includes "Properties..." in the layer context menu.

## Design

### Scope

Single file change: `src/app/layer_tree_menu.cpp`

### Change

Add "Properties..." action to the `NodeLayer` branch of `LayerTreeMenuProvider::createContextMenu()`, positioned after the zoom actions and before the rename/remove group:

```cpp
menu->addAction(QObject::tr("Properties..."), mWindow, &QgisDesktopWindow::layerProperties);
menu->addSeparator();
```

### Why `layerProperties()` (public slot)

- Already exists as a public slot on `QgisDesktopWindow`
- Uses `selectedLayers()` internally — right-click selects the layer, so this works naturally
- No header changes needed
- Consistent with menu bar behavior

### Menu Layout (NodeLayer branch, after change)

1. Zoom to Layers
2. Zoom to Native Resolution (raster only)
3. **Properties...** ← new
4. --- separator --- ← new
5. Rename Layer
6. Show Feature Count
7. Remove Layer
8. --- separator ---
9. Move to Top / Move to Bottom / Group Selected

## Verification

1. Build: `cd build && cmake .. && make -j$(nproc)`
2. Launch, add a raster or vector layer
3. Right-click layer → verify "Properties..." appears
4. Click "Properties..." → verify dialog opens
5. Verify other menu items still work
