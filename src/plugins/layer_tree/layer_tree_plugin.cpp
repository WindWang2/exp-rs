#include "layer_tree_plugin.h"

#include <QMenu>
#include <QAction>
#include <QIcon>

#include <qgsmapcanvas.h>
#include <layertree/qgslayertreeview.h>
#include <layertree/qgslayertreemodel.h>
#include <layertree/qgslayertreeviewdefaultactions.h>
#include <qgsproject.h>
#include <qgslayertree.h>
#include <qgslayertreegroup.h>
#include <qgslayertreelayer.h>
#include <qgslayertreenode.h>
#include <qgsmaplayer.h>
#include <qgsrasterlayer.h>
#include <qgsdockwidget.h>

#include "app/active_view_host.h"
#include "app/python/sicnu_app_interface.h"

LayerTreePlugin::LayerTreePlugin(QObject *parent)
    : QObject(parent)
{
}

QIcon LayerTreePlugin::icon() const
{
    return QIcon::fromTheme("layer-tree");
}

bool LayerTreePlugin::initialize(SicnuAppInterface *iface)
{
    if (!iface) {
        qWarning() << "LayerTreePlugin: no SicnuAppInterface provided, initialization failed";
        return false;
    }

    ActiveViewHost *viewHost = iface->activeViewHost();
    QgsLayerTreeView *layerTree = viewHost ? viewHost->layerTreeView() : nullptr;
    if (!layerTree) {
        qWarning() << "LayerTreePlugin: no layer tree view available, initialization failed";
        return false;
    }

    QgsProject *project = QgsProject::instance();
    QgsLayerTree *root = project->layerTreeRoot();

    m_model = new QgsLayerTreeModel(root, this);
    m_model->setFlag(QgsLayerTreeModel::ShowLegend);
    m_model->setFlag(QgsLayerTreeModel::ShowLegendAsTree);
    m_model->setFlag(QgsLayerTreeModel::UseEmbeddedWidgets);
    m_model->setFlag(QgsLayerTreeModel::UseTextFormatting);
    m_model->setFlag(QgsLayerTreeModel::AllowNodeReorder);
    m_model->setFlag(QgsLayerTreeModel::AllowNodeRename);
    m_model->setFlag(QgsLayerTreeModel::AllowNodeChangeVisibility);
    m_model->setFlag(QgsLayerTreeModel::AllowLegendChangeState);
    m_model->setFlag(QgsLayerTreeModel::ActionHierarchical);

    layerTree->setLayerTreeModel(m_model);
    layerTree->setModel(m_model);
    layerTree->expandAll();

    qDebug() << "LayerTreePlugin initialized";
    return true;
}

void LayerTreePlugin::unload()
{
    qDebug() << "LayerTreePlugin unloaded";
}

QWidget *LayerTreePlugin::createWidget(QWidget *parent)
{
    Q_UNUSED(parent);
    return nullptr;
}

QList<QAction*> LayerTreePlugin::menuActions()
{
    QList<QAction*> actions;

    QAction *addRaster = new QAction(tr("Add Raster Layer..."), this);
    connect(addRaster, &QAction::triggered, this, [this]() {
        // This will be handled by the main window
    });
    actions.append(addRaster);

    QAction *addVector = new QAction(tr("Add Vector Layer..."), this);
    connect(addVector, &QAction::triggered, this, [this]() {
        // This will be handled by the main window
    });
    actions.append(addVector);

    return actions;
}
