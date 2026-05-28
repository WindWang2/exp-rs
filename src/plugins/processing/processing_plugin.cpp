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

#include "src/processing/sicnunativealgorithms.h"

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

    // Register processing algorithms
    QgsApplication::processingRegistry()->addProvider(new SicnuNativeAlgorithms());

    qDebug() << "ProcessingPlugin initialized";
    return true;
}

void ProcessingPlugin::unload()
{
    qDebug() << "ProcessingPlugin unloaded";
}

QWidget *ProcessingPlugin::createWidget(QWidget *parent)
{
    QDockWidget *dock = new QDockWidget("Processing Toolbox", parent);
    dock->setObjectName("processingDock");
    dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    m_toolboxView = new QgsProcessingToolboxTreeView(dock);
    m_toolboxView->setRegistry(QgsApplication::processingRegistry());
    dock->setWidget(m_toolboxView);

    // Connect double-click to open algorithm dialog
    connect(m_toolboxView, &QgsProcessingToolboxTreeView::doubleClicked, this, [this](const QModelIndex &index) {
        const QgsProcessingAlgorithm *alg = m_toolboxView->algorithmForIndex(index);
        if (!alg) return;

        QgsProcessingAlgorithm *algorithm = alg->create();
        if (!algorithm) return;

        class SimpleAlgorithmDialog : public QgsProcessingAlgorithmDialogBase
        {
        public:
            using QgsProcessingAlgorithmDialogBase::QgsProcessingAlgorithmDialogBase;
            QVariantMap createProcessingParameters(Flags = Flags()) override { return QVariantMap(); }
            QgsProcessingContext *processingContext() override { return &mContext; }
        private:
            QgsProcessingContext mContext;
        };

        SimpleAlgorithmDialog *dlg = new SimpleAlgorithmDialog(m_toolboxView);
        dlg->setAlgorithm(algorithm);
        dlg->exec();
        dlg->deleteLater();
    });

    return dock;
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
