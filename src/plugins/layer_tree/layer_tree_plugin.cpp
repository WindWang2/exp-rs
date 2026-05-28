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

LayerTreePlugin::LayerTreePlugin(QObject *parent)
    : QObject(parent)
{
}

QIcon LayerTreePlugin::icon() const
{
    return QIcon::fromTheme("layer-tree");
}

bool LayerTreePlugin::initialize(QgsMapCanvas *canvas, QgsLayerTreeView *layerTree)
{
    m_canvas = canvas;
    m_layerTree = layerTree;

    if (!m_layerTree) {
        qWarning() << "LayerTreePlugin: layerTree is null";
        return false;
    }

    // Set up the layer tree model
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

    m_layerTree->setLayerTreeModel(m_model);
    m_layerTree->setModel(m_model);
    m_layerTree->expandAll();

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
