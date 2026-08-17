#include "layer_tree_menu.h"
#include "active_view_host.h"

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

#include <QPointer>
#include <QFileDialog>

LayerTreeMenuProvider::LayerTreeMenuProvider(QgsLayerTreeView *view, ActiveViewHost *activeViewHost)
    : mView(view), m_activeViewHost(activeViewHost) {}

QMenu *LayerTreeMenuProvider::createContextMenu()
{
    QMenu *menu = new QMenu();
    QModelIndex index = mView ? mView->currentIndex() : QModelIndex();
    QgsLayerTreeNode *node = index.isValid() ? mView->index2node(index) : nullptr;
    QPointer<ActiveViewHost> hostPtr(m_activeViewHost);

    auto addRasterAction = [hostPtr]() {
        if (hostPtr) {
            const QString file = QFileDialog::getOpenFileName(
                nullptr, QObject::tr("Add Raster Layer"), QString(),
                QObject::tr("Raster Formats (*.tif *.tiff *.img *.dat *.pix *.vrt *.nc *.hdf *.h5 *.png *.jpg *.jpeg);;All Files (*.*)") );
            if (!file.isEmpty())
                hostPtr->openRasterPath(file);
        }
    };

    auto addVectorAction = [hostPtr]() {
        if (hostPtr) {
            const QString file = QFileDialog::getOpenFileName(
                nullptr, QObject::tr("Add Vector Layer"), QString(),
                QObject::tr("Vector Formats (*.shp *.gpkg *.geojson *.kml *.tab *.mif);;All Files (*.*)") );
            if (!file.isEmpty())
                hostPtr->openVectorPath(file);
        }
    };

    if (!node) {
        if (m_activeViewHost) {
            menu->addAction(QObject::tr("Add Raster Layer..."), menu, addRasterAction);
            menu->addAction(QObject::tr("Add Vector Layer..."), menu, addVectorAction);
        }
        menu->addSeparator();
        if (mView) {
            menu->addAction(mView->defaultActions()->actionAddGroup());
        }
        return menu;
    }

    QgsLayerTreeViewDefaultActions *defActions = mView->defaultActions();

    if (node->nodeType() == QgsLayerTreeNode::NodeGroup) {
        menu->addAction(defActions->actionZoomToGroup(nullptr));
        menu->addAction(defActions->actionRenameGroupOrLayer());
        menu->addAction(defActions->actionRemoveGroupOrLayer());
        menu->addSeparator();
        menu->addAction(defActions->actionAddGroup());
        menu->addAction(defActions->actionMutuallyExclusiveGroup());
    } else if (node->nodeType() == QgsLayerTreeNode::NodeLayer) {
        QgsLayerTreeLayer *layerNode = static_cast<QgsLayerTreeLayer *>(node);
        QgsMapLayer *layer = layerNode->layer();
        QPointer<QgsMapLayer> layerPtr(layer);

        if (m_activeViewHost) {
            QAction *zoomAction = menu->addAction(QObject::tr("Zoom to Layer"));
            QObject::connect(zoomAction, &QAction::triggered, menu, [hostPtr, layerPtr]() {
                if (hostPtr && layerPtr) {
                    hostPtr->zoomToLayer(layerPtr.data());
                }
            });
        }

        if (layer && layer->type() == Qgis::LayerType::Raster) {
            QAction *zoomNative = menu->addAction(QObject::tr("Zoom to Native Resolution (1:1)"));
            QObject::connect(zoomNative, &QAction::triggered, menu, [hostPtr, layerPtr]() {
                if (hostPtr && layerPtr) {
                    hostPtr->zoomToNativeResolution(layerPtr.data());
                }
            });
        }

        if (m_activeViewHost) {
            menu->addAction(QObject::tr("Properties..."), menu, [hostPtr, layerPtr]() {
                if (hostPtr && layerPtr) {
                    hostPtr->showLayerProperties(layerPtr.data());
                }
            });
        }
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
    if (m_activeViewHost) {
        menu->addAction(QObject::tr("Add Raster Layer..."), menu, addRasterAction);
        menu->addAction(QObject::tr("Add Vector Layer..."), menu, addVectorAction);
    }

    return menu;
}
