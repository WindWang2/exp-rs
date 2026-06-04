# Layer Properties Context Menu Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add "Properties..." to the layer tree right-click context menu.

**Architecture:** Single line addition in `LayerTreeMenuProvider::createContextMenu()`, reusing the existing `QgisDesktopWindow::layerProperties()` public slot.

**Tech Stack:** C++17, Qt6, QGIS framework

---

### Task 1: Add Properties action to layer context menu

**Files:**
- Modify: `src/app/layer_tree_menu.cpp:65` (after zoom actions, before rename)

- [ ] **Step 1: Add Properties action and separator**

In `src/app/layer_tree_menu.cpp`, inside the `NodeLayer` branch (line 65), insert after the zoom-to-native-resolution block and before `actionRenameGroupOrLayer()`:

```cpp
        menu->addAction(QObject::tr("Properties..."), mWindow, &QgisDesktopWindow::layerProperties);
        menu->addSeparator();
```

The full `NodeLayer` branch should read:

```cpp
    } else if (node->nodeType() == QgsLayerTreeNode::NodeLayer) {
        QgsLayerTreeLayer *layerNode = static_cast<QgsLayerTreeLayer *>(node);
        QgsMapLayer *layer = layerNode->layer();

        menu->addAction(defActions->actionZoomToLayers(mCanvas));

        if (layer && layer->type() == Qgis::LayerType::Raster) {
            QAction *zoomNative = menu->addAction(QObject::tr("Zoom to Native Resolution (1:1)"));
            QObject::connect(zoomNative, &QAction::triggered, [this, layer]() {
                QgsRasterLayer *rl = qobject_cast<QgsRasterLayer *>(layer);
                if (rl) {
                    double xRes = rl->rasterUnitsPerPixelX();
                    double yRes = rl->rasterUnitsPerPixelY();
                    QgsRectangle ext = rl->extent();
                    double cx = (ext.xMinimum() + ext.xMaximum()) / 2.0;
                    double cy = (ext.yMinimum() + ext.yMaximum()) / 2.0;
                    double w = mCanvas->width() * xRes;
                    double h = mCanvas->height() * yRes;
                    mCanvas->setExtent(QgsRectangle(cx - w/2, cy - h/2, cx + w/2, cy + h/2));
                    mCanvas->refresh();
                }
            });
        }

        menu->addAction(QObject::tr("Properties..."), mWindow, &QgisDesktopWindow::layerProperties);
        menu->addSeparator();
        menu->addAction(defActions->actionRenameGroupOrLayer());
        menu->addAction(defActions->actionShowFeatureCount());
        menu->addAction(defActions->actionRemoveGroupOrLayer());
        menu->addSeparator();
        menu->addAction(defActions->actionMoveToTop());
        menu->addAction(defActions->actionMoveToBottom());
        menu->addAction(defActions->actionGroupSelected());
    }
```

- [ ] **Step 2: Build and verify**

```bash
cd /home/kevin/projects/exp-rs/build && cmake .. && make -j$(nproc)
```

Expected: Build succeeds with no errors.

- [ ] **Step 3: Manual verification**

1. Launch: `./build/sicnu_geo_rs`
2. Add a raster or vector layer
3. Right-click the layer in the tree
4. Verify "Properties..." appears after zoom actions, before "Rename Layer"
5. Click "Properties..." — verify the layer properties dialog opens
6. Verify other menu items (Rename, Remove, Move, etc.) still work

- [ ] **Step 4: Commit**

```bash
git add src/app/layer_tree_menu.cpp
git commit -m "feat(layer-tree): add Properties to layer context menu"
```
