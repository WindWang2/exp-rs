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
            QAction *actRaster = menu->addAction(QObject::tr("Add Raster Layer..."), menu, addRasterAction);
            actRaster->setToolTip(QObject::tr("打开并加载多波段遥感栅格影像图层"));
            actRaster->setStatusTip(QObject::tr("添加栅格影像图层到当前工程"));
            QAction *actVector = menu->addAction(QObject::tr("Add Vector Layer..."), menu, addVectorAction);
            actVector->setToolTip(QObject::tr("打开并加载矢量要素图层 (Shapefile / GeoPackage)"));
            actVector->setStatusTip(QObject::tr("添加矢量图层到当前工程"));
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
            zoomAction->setToolTip(QObject::tr("缩放画布以完整显示该图层的空间范围"));
            zoomAction->setStatusTip(QObject::tr("缩放到选中图层范围"));
            QObject::connect(zoomAction, &QAction::triggered, menu, [hostPtr, layerPtr]() {
                if (hostPtr && layerPtr) {
                    hostPtr->zoomToLayer(layerPtr.data());
                }
            });
        }

        if (layer && layer->type() == Qgis::LayerType::Raster) {
            QAction *zoomNative = menu->addAction(QObject::tr("Zoom to Native Resolution (1:1)"));
            zoomNative->setToolTip(QObject::tr("以 1:1 原始像元分辨率显示当前栅格"));
            zoomNative->setStatusTip(QObject::tr("缩放到原始像元分辨率"));
            QObject::connect(zoomNative, &QAction::triggered, menu, [hostPtr, layerPtr]() {
                if (hostPtr && layerPtr) {
                    hostPtr->zoomToNativeResolution(layerPtr.data());
                }
            });
        }

        if (m_activeViewHost) {
            QAction *propsAction = menu->addAction(QObject::tr("Properties..."), menu, [hostPtr, layerPtr]() {
                if (hostPtr && layerPtr) {
                    hostPtr->showLayerProperties(layerPtr.data());
                }
            });
            propsAction->setToolTip(QObject::tr("打开图层属性对话框 (波段渲染、透明度、元数据与投影)"));
            propsAction->setStatusTip(QObject::tr("查看和修改图层属性"));
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
        QAction *actRaster = menu->addAction(QObject::tr("Add Raster Layer..."), menu, addRasterAction);
        actRaster->setToolTip(QObject::tr("打开并加载多波段遥感栅格影像图层"));
        actRaster->setStatusTip(QObject::tr("添加栅格影像图层到当前工程"));
        QAction *actVector = menu->addAction(QObject::tr("Add Vector Layer..."), menu, addVectorAction);
        actVector->setToolTip(QObject::tr("打开并加载矢量要素图层 (Shapefile / GeoPackage)"));
        actVector->setStatusTip(QObject::tr("添加矢量图层到当前工程"));
    }

    return menu;
}
