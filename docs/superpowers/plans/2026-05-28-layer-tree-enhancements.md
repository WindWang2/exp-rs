# Layer Tree Enhancements Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the custom layer tree context menu with QGIS native MenuProvider, add layer groups, and verify drag-and-drop and visibility checkboxes work.

**Architecture:** Implement `QgsLayerTreeViewMenuProvider` to use QGIS's pre-built default actions. Add layer grouping logic to `loadRasterLayer()` and `loadVectorLayer()`. Update `refreshCanvasLayers()` to respect tree order.

**Tech Stack:** C++17, Qt6, QGIS C++ API (QgsLayerTreeView, QgsLayerTreeViewDefaultActions, QgsLayerTreeGroup)

**File:** All changes are in `main.cpp`.

---

### Task 1: Add Include and MenuProvider Class

**Files:**
- Modify: `main.cpp:47-58` (includes section)
- Modify: `main.cpp:86` (before QgisDesktopWindow class)

- [ ] **Step 1: Add the include for DefaultActions**

Add after line 49 (`#include <layertree/qgslayertreemodel.h>`):

```cpp
#include <layertree/qgslayertreeviewdefaultactions.h>
```

- [ ] **Step 2: Move LayerTreeMenuProvider class AFTER QgisDesktopWindow**

The `LayerTreeMenuProvider` class must be defined AFTER `QgisDesktopWindow` because its `createContextMenu()` method uses `static_cast<QgisDesktopWindow *>` which requires the complete type.

Add the class at the end of the file, after the `QgisDesktopWindow` closing brace but before `main()`:

```cpp
class LayerTreeMenuProvider : public QgsLayerTreeViewMenuProvider
{
public:
    LayerTreeMenuProvider(QgsLayerTreeView *view, QgsMapCanvas *canvas, QgisDesktopWindow *window)
        : mView(view), mCanvas(canvas), mWindow(window) {}

    QMenu *createContextMenu() override
    {
        QMenu *menu = new QMenu();
        QModelIndex index = mView->currentIndex();
        QgsLayerTreeNode *node = mView->layerTreeModel()->index2node(index);

        if (!node) {
            menu->addAction(QObject::tr("Add Raster Layer..."), mWindow, &QgisDesktopWindow::addRasterLayer);
            menu->addAction(QObject::tr("Add Vector Layer..."), mWindow, &QgisDesktopWindow::addVectorLayer);
            menu->addSeparator();
            menu->addAction(mView->defaultActions()->actionAddGroup());
            return menu;
        }

        QgsLayerTreeViewDefaultActions *defActions = mView->defaultActions();

        if (node->nodeType() == QgsLayerTreeNode::NodeGroup) {
            menu->addAction(defActions->actionZoomToGroup(mCanvas));
            menu->addAction(defActions->actionRenameGroupOrLayer());
            menu->addAction(defActions->actionRemoveGroupOrLayer());
            menu->addSeparator();
            menu->addAction(defActions->actionAddGroup());
            menu->addAction(defActions->actionMutuallyExclusiveGroup());
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

            menu->addAction(defActions->actionRenameGroupOrLayer());
            menu->addAction(defActions->actionShowFeatureCount());
            menu->addAction(defActions->actionRemoveGroupOrLayer());
            menu->addSeparator();
            menu->addAction(defActions->actionMoveToTop());
            menu->addAction(defActions->actionMoveToBottom());
            menu->addAction(defActions->actionGroupSelected());
        }

        menu->addSeparator();
        menu->addAction(QObject::tr("Add Raster Layer..."), mWindow, &QgisDesktopWindow::addRasterLayer);
        menu->addAction(QObject::tr("Add Vector Layer..."), mWindow, &QgisDesktopWindow::addVectorLayer);

        return menu;
    }

private:
    QgsLayerTreeView *mView;
    QgsMapCanvas *mCanvas;
    QgisDesktopWindow *mWindow;
};
```

Note: The constructor takes `QgisDesktopWindow *` directly (not `QMainWindow *`), and the menu actions use `&QgisDesktopWindow::addRasterLayer` directly (no cast needed).

- [ ] **Step 3: Build to verify**

Run: `cd /home/kevin/projects/exp-rs && cmake --build build -j$(nproc) 2>&1 | tail -10`
Expected: Build succeeds

- [ ] **Step 4: Commit**

```bash
git add main.cpp
git commit -m "feat(layer-tree): add LayerTreeMenuProvider with QGIS default actions"
```

---

### Task 2: Wire Up MenuProvider and Remove Old Context Menu

**Files:**
- Modify: `main.cpp` (setupDockWidgets, setupMapCanvas, showLayerTreeContextMenu, member variables)

- [ ] **Step 1: Remove old context menu setup from setupDockWidgets()**

Delete these lines from `setupDockWidgets()` (around line 272-275):
```cpp
        // Enable context menu
        m_layerTreeView->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(m_layerTreeView, &QgsLayerTreeView::customContextMenuRequested,
                this, &QgisDesktopWindow::showLayerTreeContextMenu);
```

- [ ] **Step 2: Add MenuProvider setup in setupMapCanvas()**

In `setupMapCanvas()`, after `m_mapCanvas->setMapTool(m_panTool);` (line 165), add:
```cpp
        // Set up native QGIS context menu for layer tree
        m_layerTreeMenuProvider = new LayerTreeMenuProvider(m_layerTreeView, m_mapCanvas, this);
        m_layerTreeView->setMenuProvider(m_layerTreeMenuProvider);
```

Note: `this` is `QgisDesktopWindow *` which matches the constructor parameter type.

- [ ] **Step 3: Add member variable**

In the private member variables section (around line 856), add:
```cpp
    LayerTreeMenuProvider *m_layerTreeMenuProvider = nullptr;
```

Note: Since `LayerTreeMenuProvider` is defined after `QgisDesktopWindow`, you need a forward declaration at the top of the file (before `QgisDesktopWindow`):
```cpp
class LayerTreeMenuProvider;
```

- [ ] **Step 4: Remove old showLayerTreeContextMenu method**

Delete the entire `showLayerTreeContextMenu()` method (lines 666-744).

- [ ] **Step 5: Build to verify**

Run: `cd /home/kevin/projects/exp-rs && cmake --build build -j$(nproc) 2>&1 | tail -10`
Expected: Build succeeds

- [ ] **Step 6: Commit**

```bash
git add main.cpp
git commit -m "feat(layer-tree): wire up MenuProvider, remove old context menu"
```

---

### Task 3: Add Layer Groups

**Files:**
- Modify: `main.cpp` (loadRasterLayer, loadVectorLayer, add helper method)

- [ ] **Step 1: Add helper method**

Add in the private section (before `loadRasterLayer`):
```cpp
    QgsLayerTreeGroup *findOrCreateGroup(const QString &name)
    {
        QgsLayerTree *root = QgsProject::instance()->layerTreeRoot();
        QgsLayerTreeGroup *group = root->findGroup(name);
        if (!group) {
            group = root->addGroup(name);
        }
        return group;
    }
```

- [ ] **Step 2: Modify loadRasterLayer() to add to group**

After `QgsProject::instance()->addMapLayer(layer);` in `loadRasterLayer()`, add:
```cpp
        QgsLayerTreeGroup *group = findOrCreateGroup("Raster Layers");
        group->addLayer(layer);
```

- [ ] **Step 3: Modify loadVectorLayer() to add to group**

After `QgsProject::instance()->addMapLayer(layer);` in `loadVectorLayer()`, add:
```cpp
        QgsLayerTreeGroup *group = findOrCreateGroup("Vector Layers");
        group->addLayer(layer);
```

- [ ] **Step 4: Build to verify**

Run: `cd /home/kevin/projects/exp-rs && cmake --build build -j$(nproc) 2>&1 | tail -10`
Expected: Build succeeds

- [ ] **Step 5: Commit**

```bash
git add main.cpp
git commit -m "feat(layer-tree): auto-group raster and vector layers"
```

---

### Task 4: Update Canvas Layer Order and Auto-Load

**Files:**
- Modify: `main.cpp` (refreshCanvasLayers, auto-load in main())

- [ ] **Step 1: Update refreshCanvasLayers() to use tree order**

Replace `refreshCanvasLayers()`:
```cpp
    void refreshCanvasLayers()
    {
        QgsLayerTree *root = QgsProject::instance()->layerTreeRoot();
        QList<QgsMapLayer*> layers = root->layerOrder();
        m_mapCanvas->setLayers(layers);
    }
```

- [ ] **Step 2: Update auto-load to use groups**

Replace the auto-load block in `main()`:
```cpp
    // Auto-load sample data if available
    QString samplePath = "/home/kevin/projects/exp-rs/data/sample_crops.tif";
    if (QFileInfo::exists(samplePath)) {
        QTimer::singleShot(500, [&window, samplePath]() {
            auto *layer = new QgsRasterLayer(samplePath, "sample_crops");
            if (layer->isValid()) {
                QgsProject::instance()->addMapLayer(layer);
                QgsLayerTree *root = QgsProject::instance()->layerTreeRoot();
                QgsLayerTreeGroup *group = root->findGroup("Raster Layers");
                if (!group) group = root->addGroup("Raster Layers");
                group->addLayer(layer);
                window.mapCanvas()->setExtent(layer->extent());
                window.mapCanvas()->setLayers(root->layerOrder());
                window.mapCanvas()->refresh();
            }
        });
    }
```

- [ ] **Step 3: Build to verify**

Run: `cd /home/kevin/projects/exp-rs && cmake --build build -j$(nproc) 2>&1 | tail -10`
Expected: Build succeeds

- [ ] **Step 4: Commit**

```bash
git add main.cpp
git commit -m "feat(layer-tree): use tree layer order and group auto-loaded layers"
```

---

### Task 5: Final Verification

**Files:**
- None (verification only)

- [ ] **Step 1: Clean build**

Run: `cd /home/kevin/projects/exp-rs && cmake --build build -j$(nproc) 2>&1 | grep -E "error:" | head -10`
Expected: No errors

- [ ] **Step 2: Run the application**

Run: `cd /home/kevin/projects/exp-rs && timeout 5 ./build/sicnu_geo_rs 2>&1 || true`
Expected: No crash

- [ ] **Step 3: Verify features**

Launch the app and verify:
1. Layer tree shows "Raster Layers" group with sample_crops.tif inside
2. Checkbox next to layer toggles visibility
3. Right-click on layer shows full menu (Zoom, Rename, Remove, Feature Count, etc.)
4. Right-click on group shows group menu (Add Group, Mutually Exclusive, etc.)
5. Right-click on empty area shows Add Layer + Add Group
6. Drag-and-drop reorders layers
7. Double-click opens Layer Properties
