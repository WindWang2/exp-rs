// main_window_connections.cpp — Signal/slot wiring and canvas updates
#include "main_window.h"

#include "data_project_serializer.h"
#include "active_view_host.h"
#include "layer_tree_menu.h"
#include "project_context.h"
#include "widgets/histogram_stretch_widget.h"
#include "widgets/band_composition_rail.h"
#include "processing/framework/task_center.h"

#include <QStatusBar>
#include <QSignalBlocker>
#include <QMessageBox>
#include <QStringList>
#include <cmath>

#include <qgsapplication.h>
#include <qgsproject.h>
#include <qgsrasterlayer.h>
#include <qgsvectorlayer.h>
#include <qgslayertreeview.h>
#include <qgslayertreelayer.h>
#include <qgslayertreenode.h>
#include <qgslayertreegroup.h>
#include <qgsmapcanvas.h>
#include <qgsmaptoolidentify.h>
#include <qgscoordinatereferencesystem.h>

#include <QPainter>

namespace
{

QString formatProjectDiagnostics(
    const QVector<sicnu::data::Diagnostic> &diagnostics )
{
    QStringList details;
    details.reserve( diagnostics.size() );
    for ( const sicnu::data::Diagnostic &diagnostic : diagnostics )
    {
        details.append( QStringLiteral( "[%1] %2" )
                            .arg( diagnostic.code, diagnostic.message ) );
    }
    return details.join( '\n' );
}

} // namespace

void QgisDesktopWindow::setupConnections()
{
    // Map canvas signals
    connect(m_mapCanvas, &QgsMapCanvas::xyCoordinates,
            this, &QgisDesktopWindow::showCoordinates);
    connect(m_mapCanvas, &QgsMapCanvas::scaleChanged,
            this, &QgisDesktopWindow::updateScale);
    connect(m_mapCanvas, &QgsMapCanvas::extentsChanged,
            this, &QgisDesktopWindow::updateExtents);
    connect(m_mapCanvas, &QgsMapCanvas::renderComplete,
            this, &QgisDesktopWindow::onRenderComplete);
    // Band rail + status bar: follow active map layer.
    auto syncActiveLayerChrome = [this]( QgsMapLayer *layer ) {
        syncStatusBarLayer( layer );
        if ( !m_bandRail )
            return;
        if ( auto *rl = qobject_cast<QgsRasterLayer *>( layer ) )
            m_bandRail->setRasterLayer( rl );
        else
            m_bandRail->setRasterLayer( nullptr );
    };
    connect( m_mapCanvas, &QgsMapCanvas::currentLayerChanged,
             this, [syncActiveLayerChrome]( QgsMapLayer *layer ) {
                 syncActiveLayerChrome( layer );
             } );
    connect( m_mapCanvas, &QgsMapCanvas::layersChanged, this, [this, syncActiveLayerChrome]() {
        if ( !m_mapCanvas )
            return;
        QgsMapLayer *layer = m_mapCanvas->currentLayer();
        if ( !layer )
        {
            for ( QgsMapLayer *l : m_mapCanvas->layers() )
            {
                if ( l )
                {
                    layer = l;
                    break;
                }
            }
        }
        // Prefer raster for band rail when current is not raster.
        if ( m_bandRail )
        {
            if ( auto *rl = qobject_cast<QgsRasterLayer *>( layer ) )
                m_bandRail->setRasterLayer( rl );
            else
            {
                QgsRasterLayer *firstRaster = nullptr;
                for ( QgsMapLayer *l : m_mapCanvas->layers() )
                {
                    if ( auto *rl = qobject_cast<QgsRasterLayer *>( l ) )
                    {
                        firstRaster = rl;
                        break;
                    }
                }
                m_bandRail->setRasterLayer( firstRaster );
            }
        }
        syncStatusBarLayer( layer );
    } );
    m_renderTimer.start();

    // QgsTaskManager → Ready line (legacy tasks)
    connect( QgsApplication::taskManager(), &QgsTaskManager::statusChanged,
             this, [this]( long taskId, int status ) {
                 Q_UNUSED( taskId );
                 Q_UNUSED( status );
                 refreshStatusTaskSummary();
             } );

    // Task Center → Ready line (product job queue)
    connect( &sicnu::TaskCenter::instance(), &sicnu::TaskCenter::taskAdded,
             this, [this]( const sicnu::AlgorithmTaskInfo & ) { refreshStatusTaskSummary(); } );
    connect( &sicnu::TaskCenter::instance(), &sicnu::TaskCenter::taskUpdated,
             this, [this]( const sicnu::AlgorithmTaskInfo & ) { refreshStatusTaskSummary(); } );

    // Identify tool results
    connect(m_identifyTool, &CustomIdentifyTool::identifyCompleted,
            this, &QgisDesktopWindow::onIdentifyResults);

    // Project signals
    connect(QgsProject::instance(), &QgsProject::readProject,
            this, &QgisDesktopWindow::onProjectRead);
    connect(QgsProject::instance(), &QgsProject::writeProject,
            this, &QgisDesktopWindow::onProjectWrite);
}

void QgisDesktopWindow::initLayerTree()
{
    // Delegate layer tree model + bridge setup to ActiveViewHost
    m_activeViewHost->initLayerTree();
    m_layerTreeModel = m_activeViewHost->layerTreeModel();

    // Connect layer tree signals (UI events remain in the window)
    connect(m_layerTreeView, &QgsLayerTreeView::clicked,
            this, &QgisDesktopWindow::onLayerTreeClicked);
    connect(m_layerTreeView, &QgsLayerTreeView::doubleClicked,
            this, &QgisDesktopWindow::onLayerTreeDoubleClicked);

    // Connect project signals for CRS updates
    connect(QgsProject::instance(), &QgsProject::crsChanged,
            this, &QgisDesktopWindow::updateCrsDisplay);

    // Set up native QGIS context menu for layer tree
    m_layerTreeMenuProvider = new LayerTreeMenuProvider(m_layerTreeView, m_activeViewHost.get());
    m_layerTreeView->setMenuProvider(m_layerTreeMenuProvider);
}
void QgisDesktopWindow::showCoordinates(const QgsPointXY &point)
{
    m_coordinatesLabel->setText(QString("%1, %2")
        .arg(point.x(), 0, 'f', 2)
        .arg(point.y(), 0, 'f', 2));
}

void QgisDesktopWindow::updateScale()
{
    const double s = m_mapCanvas ? m_mapCanvas->scale() : 0.0;
    QString scaleText;
    // Guard invalid / huge scales (NaN, Inf, or overflow of int) → "—"
    if ( !std::isfinite( s ) || s <= 0.0 || s > 1.0e12 )
        scaleText = QStringLiteral( "—" );
    else
        scaleText = QStringLiteral( "1:%1" ).arg( static_cast<qint64>( std::llround( s ) ) );

    if ( m_scaleLabel )
        m_scaleLabel->setText( tr( "比例 %1" ).arg( scaleText ) );
}

void QgisDesktopWindow::updateExtents()
{
    QgsRectangle ext = m_mapCanvas->extent();
    statusBar()->showMessage(
        QString("Extent: %1,%2 - %3,%4")
            .arg(ext.xMinimum(), 0, 'f', 1)
            .arg(ext.yMinimum(), 0, 'f', 1)
            .arg(ext.xMaximum(), 0, 'f', 1)
            .arg(ext.yMaximum(), 0, 'f', 1),
        5000);
}

void QgisDesktopWindow::updateCrsDisplay()
{
    QgsCoordinateReferenceSystem crs = QgsProject::instance()->crs();
    if ( m_crsLabel )
        m_crsLabel->setText( crs.authid() );
    if ( m_crsSelector )
        m_crsSelector->setCrs( crs );
}

void QgisDesktopWindow::syncStatusBarLayer( QgsMapLayer *layer )
{
    if ( !layer && m_mapCanvas )
        layer = m_mapCanvas->currentLayer();

    if ( m_layerStatusLabel )
    {
        if ( layer && layer->isValid() )
        {
            m_layerStatusLabel->setText( layer->name() );
            m_layerStatusLabel->setToolTip( layer->name() );
        }
        else
        {
            m_layerStatusLabel->setText( tr( "无图层" ) );
            m_layerStatusLabel->setToolTip( tr( "当前活动图层" ) );
        }
    }

    if ( m_statusOpacitySlider )
    {
        const int pct = layer
                          ? qBound( 0, static_cast<int>( std::lround( layer->opacity() * 100.0 ) ), 100 )
                          : 100;
        const QSignalBlocker blocker( m_statusOpacitySlider );
        m_statusOpacitySlider->setValue( pct );
        m_statusOpacitySlider->setEnabled( layer != nullptr );
        if ( m_statusOpacityValue )
            m_statusOpacityValue->setText( QStringLiteral( "%1%" ).arg( pct ) );
    }
}

void QgisDesktopWindow::refreshStatusTaskSummary()
{
    if ( !m_readyLabel )
        return;

    int running = 0;
    int queued = 0;
    const auto tasks = sicnu::TaskCenter::instance().allTasks();
    for ( const auto &t : tasks )
    {
        if ( t.status == sicnu::TaskStatus::Running )
            ++running;
        else if ( t.status == sicnu::TaskStatus::Queued || t.status == sicnu::TaskStatus::Paused )
            ++queued;
    }

    const int qgisActive = QgsApplication::taskManager()
                             ? QgsApplication::taskManager()->countActiveTasks()
                             : 0;

    if ( running > 0 || queued > 0 )
    {
        m_readyLabel->setText( tr( "运行 %1 · 排队 %2" ).arg( running ).arg( queued ) );
        m_readyLabel->setObjectName( QStringLiteral( "rsReadyBusy" ) );
        m_readyLabel->setStyleSheet( QStringLiteral( "color: #B58100; font-weight: 600;" ) );
    }
    else if ( qgisActive > 0 )
    {
        m_readyLabel->setText( tr( "Processing (%1)..." ).arg( qgisActive ) );
        m_readyLabel->setObjectName( QStringLiteral( "rsReadyBusy" ) );
        m_readyLabel->setStyleSheet( QStringLiteral( "color: #B58100; font-weight: 600;" ) );
    }
    else
    {
        m_readyLabel->setText( tr( "Ready" ) );
        m_readyLabel->setObjectName( QStringLiteral( "rsReadyLabel" ) );
        m_readyLabel->setStyleSheet( QString() );
    }
}

void QgisDesktopWindow::onRenderComplete(QPainter *painter)
{
    Q_UNUSED(painter);
    // Display render time
    if (m_renderTimeLabel)
    {
        qint64 elapsed = m_renderTimer.elapsed();
        m_renderTimeLabel->setText(tr("Render: %1 ms").arg(elapsed));
    }
    m_renderTimer.start(); // Restart for next render

    // Update cache label with approximate memory usage from loaded raster layers
    if (m_cacheLabel)
    {
        qint64 totalBytes = 0;
        const QMap<QString, QgsMapLayer *> layers = QgsProject::instance()->mapLayers();
        for (QgsMapLayer *layer : layers)
        {
            QgsRasterLayer *rl = qobject_cast<QgsRasterLayer *>(layer);
            if (rl && rl->isValid())
            {
                totalBytes += static_cast<qint64>(rl->width()) * rl->height()
                              * rl->bandCount() * 4; // approximate: float32 per band
            }
        }
        double mb = totalBytes / (1024.0 * 1024.0);
        m_cacheLabel->setText(QString("Cache: %1 MB").arg(mb, 0, 'f', 1));
    }
}

void QgisDesktopWindow::onProjectRead(const QDomDocument &doc)
{
    if ( m_projectContext )
    {
        const sicnu::app::DataProjectSerializer serializer;
        const sicnu::data::Result<void> restored =
            serializer.read( doc, *QgsProject::instance(), *m_projectContext );
        if ( !restored )
        {
            QMessageBox::warning(
                this, tr( "Project Data" ),
                tr( "The QGIS project opened, but some SICNU data relationships "
                    "could not be restored:\n%1" )
                    .arg( formatProjectDiagnostics(
                        restored.diagnostics() ) ) );
        }
    }
    refreshCanvasLayers();
    updateCrsDisplay();

    // Sync editing UI: check if any vector layer is in edit mode
    QgsVectorLayer *activeVl = nullptr;
    const QMap<QString, QgsMapLayer *> layers = QgsProject::instance()->mapLayers();
    for (QgsMapLayer *layer : layers)
    {
        QgsVectorLayer *vl = qobject_cast<QgsVectorLayer *>(layer);
        if (vl)
        {
            if (!activeVl)
                activeVl = vl;
        }
    }
    updateEditingUI(activeVl);

    statusBar()->showMessage("Project loaded", 3000);
}

void QgisDesktopWindow::onProjectWrite(QDomDocument &doc)
{
    if ( m_projectContext )
    {
        const sicnu::app::DataProjectSerializer serializer;
        const sicnu::data::Result<void> written =
            serializer.write( doc, *m_projectContext );
        if ( !written )
        {
            QMessageBox::warning(
                this, tr( "Project Data" ),
                tr( "The project could not include the SICNU data catalog:\n%1" )
                    .arg( formatProjectDiagnostics(
                        written.diagnostics() ) ) );
        }
    }
    statusBar()->showMessage("Project saved", 2000);
}

void QgisDesktopWindow::onCrsChanged(const QgsCoordinateReferenceSystem &crs)
{
    QgsProject::instance()->setCrs(crs);
    m_mapCanvas->setDestinationCrs(crs);
    m_mapCanvas->refresh();
    updateCrsDisplay();
}

// ── Layer Tree Events ─────────────────────────────────────────────────────
void QgisDesktopWindow::onLayerTreeClicked(const QModelIndex &index)
{
    // Check if the current layer has unsaved edits before switching
    QgsVectorLayer *currentVl = currentVectorLayer();
    if (currentVl && currentVl->isEditable()) {
        QgsLayerTreeNode *node = m_layerTreeView->index2node(index);
        QgsMapLayer *newLayer = nullptr;
        if (node && node->nodeType() == QgsLayerTreeNode::NodeLayer) {
            auto *layerNode = static_cast<QgsLayerTreeLayer *>(node);
            newLayer = layerNode->layer();
        }
        // Only prompt if actually switching to a different layer
        if (newLayer != currentVl && !confirmSaveEdits(currentVl))
            return;
    }

    QgsLayerTreeNode *node = m_layerTreeView->index2node(index);
    if (node && node->nodeType() == QgsLayerTreeNode::NodeLayer) {
        QgsLayerTreeLayer *layerNode = static_cast<QgsLayerTreeLayer *>(node);
        QgsMapLayer *layer = layerNode->layer();
        m_mapCanvas->setCurrentLayer(layer);
        updateEditingUI(qobject_cast<QgsVectorLayer *>(layer));

        if ( m_histogramStretch )
        {
            if ( auto *rl = qobject_cast<QgsRasterLayer *>( layer ) )
            {
                m_histogramStretch->setRasterLayer( rl );
                if ( m_histogramStretchDock && m_histogramStretchDock->isVisible() )
                    m_histogramStretchDock->setWindowTitle(
                      tr( "显示拉伸 — %1" ).arg( rl->name() ) );
            }
        }
        if ( m_bandRail )
        {
            if ( auto *rl = qobject_cast<QgsRasterLayer *>( layer ) )
                m_bandRail->setRasterLayer( rl );
            else
                m_bandRail->setRasterLayer( nullptr );
        }
        syncStatusBarLayer( layer );
    } else {
        m_mapCanvas->setCurrentLayer(nullptr);
        updateEditingUI(nullptr);
        syncStatusBarLayer( nullptr );
    }
}

void QgisDesktopWindow::onLayerTreeDoubleClicked(const QModelIndex &index)
{
    QgsLayerTreeNode *node = m_layerTreeView->index2node(index);
    if (node && node->nodeType() == QgsLayerTreeNode::NodeLayer) {
        QgsLayerTreeLayer *layerNode = static_cast<QgsLayerTreeLayer*>(node);
        if (layerNode->layer()) {
            showLayerProperties(layerNode->layer());
        }
    }
}
