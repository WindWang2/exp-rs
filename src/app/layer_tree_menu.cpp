#include "layer_tree_menu.h"
#include "main_window.h"

// QGIS includes
#include <layertree/qgslayertreeview.h>
#include <layertree/qgslayertreemodel.h>
#include <layertree/qgslayertreeviewdefaultactions.h>
#include <qgsmapcanvas.h>
#include <qgslayertreenode.h>
#include <qgslayertreelayer.h>
#include <qgslayertreegroup.h>
#include <qgsrasterlayer.h>
#include <qgsvectorlayer.h>

LayerTreeMenuProvider::LayerTreeMenuProvider(QgsLayerTreeView *view, QgsMapCanvas *canvas, QgisDesktopWindow *window)
    : mView(view), mCanvas(canvas), mWindow(window) {}

QMenu *LayerTreeMenuProvider::createContextMenu()
{
    QMenu *menu = new QMenu();
    QModelIndex index = mView->currentIndex();
    QgsLayerTreeNode *node = index.isValid() ? mView->index2node(index) : nullptr;

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

    menu->addSeparator();
    menu->addAction(QObject::tr("Add Raster Layer..."), mWindow, &QgisDesktopWindow::addRasterLayer);
    menu->addAction(QObject::tr("Add Vector Layer..."), mWindow, &QgisDesktopWindow::addVectorLayer);

    return menu;
}
