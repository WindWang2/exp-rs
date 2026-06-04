# Layer Tree Enhancements Design

## Overview

Enhance the QGIS layer tree view in SICNU GEO RS to support visibility checkboxes, layer groups, drag-and-drop reordering, and a full context menu using QGIS native APIs.

## Current State

- `QgsLayerTreeView` + `QgsLayerTreeModel` with flags: `AllowNodeChangeVisibility`, `AllowNodeReorder`, `AllowNodeRename`, `ActionHierarchical`
- Custom `showLayerTreeContextMenu` with limited actions (Zoom, Properties, Remove, Add)
- Layers loaded directly to root node (no groups)

## Architecture

### 1. Replace Custom Context Menu with QgsLayerTreeViewMenuProvider

Create a `LayerTreeMenuProvider` class implementing `QgsLayerTreeViewMenuProvider`:

```cpp
class LayerTreeMenuProvider : public QgsLayerTreeViewMenuProvider
{
public:
    LayerTreeMenuProvider(QgsLayerTreeView *view, QgsMapCanvas *canvas)
        : mView(view), mCanvas(canvas) {}
    QMenu *createContextMenu() override;
private:
    QgsLayerTreeView *mView;
    QgsMapCanvas *mCanvas;
};
```

The `createContextMenu()` method will:
- Use `mView->defaultActions()` to get pre-built QGIS actions
- Build different menus based on node type (layer, group, empty area)
- Add custom actions (Zoom to Native Resolution for raster layers)

### 2. Layer Groups

- Auto-create "Raster Layers" and "Vector Layers" groups when loading layers
- Layers added to corresponding group via `group->addLayer(layer)`
- Users can create new groups via right-click "Add Group"
- Users can group selected items via right-click "Group Selected"

### 3. Drag-and-Drop

Already enabled via `AllowNodeReorder` flag. No code changes needed.

### 4. Visibility Checkboxes

Already enabled via `AllowNodeChangeVisibility` flag. No code changes needed.

### 5. Canvas Layer Order

Update `refreshCanvasLayers()` to use `root->layerOrder()` instead of `mapLayers().values()` so layer order matches the tree.

## Changes

### main.cpp

1. Add `LayerTreeMenuProvider` class
2. Modify `setupDockWidgets()`: replace `customContextMenuRequested` with `setMenuProvider()`
3. Modify `loadRasterLayer()` and `loadVectorLayer()`: add layers to groups
4. Modify `refreshCanvasLayers()`: use `root->layerOrder()`
5. Remove old `showLayerTreeContextMenu()` method

## Verification

- Right-click on layer: full menu with Zoom, Properties, Remove, Rename, Feature Count
- Right-click on group: Add Group, Remove Group, Rename, Zoom to Group
- Right-click on empty area: Add Raster/Vector Layer, Add Group
- Checkbox toggles layer visibility
- Drag-and-drop reorders layers
- Double-click opens Layer Properties
