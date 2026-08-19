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

bool ProcessingPlugin::initialize(SicnuAppInterface *iface)
{
    // Deliberate no-op: algorithm providers are registered in main.cpp, so
    // registering them here would duplicate registration.
    Q_UNUSED(iface);

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
    // DATAPY-12: previously dead action with empty lambda.
    return {};
}
