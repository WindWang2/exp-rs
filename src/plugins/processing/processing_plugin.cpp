#include "processing_plugin.h"

#include <QMenu>
#include <QAction>
#include <QIcon>
#include <QVBoxLayout>

#include <qgsapplication.h>
#include <qgsmapcanvas.h>
#include <layertree/qgslayertreeview.h>
#include <processing/qgsprocessingregistry.h>
#include <processing/qgsprocessingtoolboxtreeview.h>
#include <processing/qgsprocessingalgorithmdialogbase.h>
#include <qgsdockwidget.h>

#include "processing/providers/gdal_tools/provider.h"
#include "processing/providers/otb_tools/provider.h"
#include "processing/providers/qgis_algorithms/provider.h"

ProcessingPlugin::ProcessingPlugin(QObject *parent)
    : QObject(parent)
{
}

QIcon ProcessingPlugin::icon() const
{
    return QIcon::fromTheme("processing");
}

bool ProcessingPlugin::initialize(QgsMapCanvas *canvas, QgsLayerTreeView *layerTree)
{
    m_canvas = canvas;
    m_layerTree = layerTree;

    // Providers are registered in main.cpp — no duplicate registration needed

    qDebug() << "ProcessingPlugin initialized";
    return true;
}

void ProcessingPlugin::unload()
{
    qDebug() << "ProcessingPlugin unloaded";
}

QWidget *ProcessingPlugin::createWidget(QWidget *parent)
{
    // Toolbox is created in main_window.cpp — this plugin provides a no-op widget
    Q_UNUSED(parent);
    return nullptr;
}

QList<QAction*> ProcessingPlugin::menuActions()
{
    QList<QAction*> actions;

    QAction *toolbox = new QAction(tr("Processing Toolbox"), this);
    connect(toolbox, &QAction::triggered, this, [this]() {
        // Show processing toolbox dock
    });
    actions.append(toolbox);

    return actions;
}
