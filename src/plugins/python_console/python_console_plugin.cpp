#include "python_console_plugin.h"

#include <QMenu>
#include <QAction>
#include <QIcon>

#include <qgsmapcanvas.h>
#include <layertree/qgslayertreeview.h>
#include <QDockWidget>

#include "gui/python_console_widget.h"
#include "python/qgis_python.h"

PythonConsolePlugin::PythonConsolePlugin(QObject *parent)
    : QObject(parent)
{
}

QIcon PythonConsolePlugin::icon() const
{
    return QIcon::fromTheme("python");
}

bool PythonConsolePlugin::initialize(QgsMapCanvas *canvas, QgsLayerTreeView *layerTree)
{
    m_canvas = canvas;
    m_layerTree = layerTree;

    // Initialize Python
    QgisPython::instance().initialize();
    QgisPython::instance().loadBindings();

    qDebug() << "PythonConsolePlugin initialized";
    return true;
}

void PythonConsolePlugin::unload()
{
    QgisPython::instance().finalize();
    qDebug() << "PythonConsolePlugin unloaded";
}

QWidget *PythonConsolePlugin::createWidget(QWidget *parent)
{
    QDockWidget *dock = new QDockWidget("Python Console", parent);
    dock->setObjectName("pythonDock");

    auto *pythonConsole = new PythonConsoleWidget(dock);
    dock->setWidget(pythonConsole);

    return dock;
}

QList<QAction*> PythonConsolePlugin::menuActions()
{
    QList<QAction*> actions;

    QAction *console = new QAction(tr("Python Console"), this);
    connect(console, &QAction::triggered, this, [this]() {
        // Show Python console dock
    });
    actions.append(console);

    return actions;
}
