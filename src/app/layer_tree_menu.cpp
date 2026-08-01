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

LayerTreeMenuProvider::LayerTreeMenuProvider(QgsLayerTreeView *view, ActiveViewHost *activeViewHost)
    : mView(view), m_activeViewHost(activeViewHost) {}

QMenu *LayerTreeMenuProvider::createContextMenu()
{
    QMenu *menu = new QMenu();
    QModelIndex index = mView ? mView->currentIndex() : QModelIndex();
    QgsLayerTreeNode *node = index.isValid() ? mView->index2node(index) : nullptr;

    if (!node) {
        if (m_activeViewHost) {
            menu->addAction(QObject::tr("Add Raster Layer..."), [this]() {
                m_activeViewHost->openRasterPath(QString());
            });
            menu->addAction(QObject::tr("Add Vector Layer..."), [this]() {
                m_activeViewHost->openVectorPath(QString());
            });
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

        if (m_activeViewHost) {
            QAction *zoomAction = menu->addAction(QObject::tr("Zoom to Layer"));
            QObject::connect(zoomAction, &QAction::triggered, [this, layer]() {
                m_activeViewHost->zoomToLayer(layer);
            });
        }

        if (layer && layer->type() == Qgis::LayerType::Raster) {
            QAction *zoomNative = menu->addAction(QObject::tr("Zoom to Native Resolution (1:1)"));
            QObject::connect(zoomNative, &QAction::triggered, [this, layer]() {
                if (m_activeViewHost) {
                    m_activeViewHost->zoomToNativeResolution(layer);
                }
            });
        }

        if (m_activeViewHost) {
            menu->addAction(QObject::tr("Properties..."), [this, layer]() {
                m_activeViewHost->showLayerProperties(layer);
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
        menu->addAction(QObject::tr("Add Raster Layer..."), [this]() {
            m_activeViewHost->openRasterPath(QString());
        });
        menu->addAction(QObject::tr("Add Vector Layer..."), [this]() {
            m_activeViewHost->openVectorPath(QString());
        });
    }

    return menu;
}
