// main_window_connections.cpp — Signal/slot wiring and canvas updates
#include "main_window.h"

#include "data_project_serializer.h"
#include "workbench/mission_context_store.h"
#include "workbench/mission_run_authority.h"
#include "workbench/mission_run_resolver.h"
#include "workbench/mission_runtime_store.h"
#include "workbench/mission_timeline_panel.h"
#include "active_view_host.h"
#include "layer_tree_menu.h"
#include "project_context.h"
#include "widgets/histogram_stretch_widget.h"
#include "processing/framework/task_center.h"

#include <QDateTime>
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
    // Status bar: follow active map layer.
    connect( m_mapCanvas, &QgsMapCanvas::currentLayerChanged,
             this, &QgisDesktopWindow::syncStatusBarLayer );
    connect( m_mapCanvas, &QgsMapCanvas::layersChanged, this, [this]() {
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
        syncStatusBarLayer( layer );
        updateCanvasEmptyState();
        updateLayersEmptyState();
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

    connect( QgsProject::instance(), &QgsProject::layersAdded, this, [this]() {
        updateCanvasEmptyState();
        updateLayersEmptyState();
    } );
    connect( QgsProject::instance(), &QgsProject::layersRemoved, this, [this]() {
        updateCanvasEmptyState();
        updateLayersEmptyState();
        // Mission Runtime 13.0: a deleted layer is a dangling reference —
        // reconcile the task space against the live layer set so dependent
        // tasks become Stale instead of pointing at nothing.
        reconcileMissionRuntimeAfterLayerChange();
    } );
    const auto watchLayerName = [this]( QgsMapLayer *layer ) {
        if ( layer )
            connect( layer, &QgsMapLayer::nameChanged, this,
                     [this]() { syncMissionLayerDisplayNames(); } );
    };
    connect( QgsProject::instance(), &QgsProject::layerWasAdded, this, watchLayerName );
    // Layers already in the project when the shell wired up: watch them too,
    // otherwise a rename of a pre-existing layer never syncs.
    if ( QgsProject::instance() )
        for ( QgsMapLayer *layer : QgsProject::instance()->mapLayers() )
            watchLayerName( layer );

    // Project signals
    connect(QgsProject::instance(), &QgsProject::readProject,
            this, &QgisDesktopWindow::onProjectRead);
    connect(QgsProject::instance(), &QgsProject::writeProject,
            this, &QgisDesktopWindow::onProjectWrite);
    // Window title mirrors project identity + dirty state.
    connect(QgsProject::instance(), &QgsProject::isDirtyChanged,
            this, [this]( bool dirty ) {
                if ( m_projectDirty == dirty )
                    return;
                m_projectDirty = dirty;
                updateWindowTitle();
            });
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

    // Set up native QGIS context menu for layer tree (projects registry commands)
    m_layerTreeMenuProvider = new LayerTreeMenuProvider(m_layerTreeView, m_activeViewHost.get(), m_commandRegistry);
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
        m_scaleLabel->setText( tr( "Scale %1" ).arg( scaleText ) );
}

void QgisDesktopWindow::updateExtents()
{
    // Permanent coordinate/scale widgets reflect canvas view without stomping transient statusBar messages.
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
            m_layerStatusLabel->setText( tr( "No Layers" ) );
            m_layerStatusLabel->setToolTip( tr( "Current Active Layer" ) );
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
    int cancelling = 0;
    const auto tasks = sicnu::TaskCenter::instance().allTasks();
    for ( const auto &t : tasks )
    {
        if ( t.status == sicnu::TaskStatus::Running || t.status == sicnu::TaskStatus::Dispatching )
            ++running;
        else if ( t.status == sicnu::TaskStatus::Queued || t.status == sicnu::TaskStatus::Paused
                  || t.status == sicnu::TaskStatus::WaitingResource )
            ++queued;
        else if ( t.status == sicnu::TaskStatus::Cancelling )
            ++cancelling;
    }

    const int qgisActive = QgsApplication::taskManager()
                             ? QgsApplication::taskManager()->countActiveTasks()
                             : 0;

    const QString prevName = m_readyLabel->objectName();
    if ( cancelling > 0 )
    {
        m_readyLabel->setText( tr( "Cancelling %1 · Running %2 · Queued %3" ).arg( cancelling ).arg( running ).arg( queued ) );
        m_readyLabel->setObjectName( QStringLiteral( "rsReadyBusy" ) );
    }
    else if ( running > 0 || queued > 0 )
    {
        m_readyLabel->setText( tr( "Running %1 · Queued %2" ).arg( running ).arg( queued ) );
        m_readyLabel->setObjectName( QStringLiteral( "rsReadyBusy" ) );
    }
    else if ( qgisActive > 0 )
    {
        m_readyLabel->setText( tr( "Processing (%1)..." ).arg( qgisActive ) );
        m_readyLabel->setObjectName( QStringLiteral( "rsReadyBusy" ) );
    }
    else
    {
        m_readyLabel->setText( tr( "Ready" ) );
        m_readyLabel->setObjectName( QStringLiteral( "rsReadyLabel" ) );
    }
    if ( m_readyLabel->objectName() != prevName )
    {
        if ( QStyle *s = m_readyLabel->style() )
        {
            s->unpolish( m_readyLabel );
            s->polish( m_readyLabel );
        }
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

void QgisDesktopWindow::reconcileMissionRuntimeAfterLayerChange()
{
    const QString projectPath = QgsProject::instance()->fileName();
    if ( projectPath.isEmpty() || m_missionRuntime.timeline.tasks().isEmpty() )
        return;
    // The runtime must belong to the project being edited: layersRemoved also
    // fires while a project is cleared/closed, and reconciling the previous
    // project's sidecar mid-teardown would be wrong.
    if ( !m_missionRuntime.context.projectRef.isEmpty()
         && m_missionRuntime.context.projectRef != projectPath )
        return;
    sicnu::app::MissionRuntimeState state;
    QString err;
    if ( !sicnu::app::loadMissionRuntime( projectPath, QDomDocument(), state, &err ) )
        return;
    const sicnu::app::MissionRefResolver resolver =
        []( const QString &refId ) -> sicnu::app::MissionRefStatus {
        sicnu::app::MissionRefStatus status;
        if ( !QgsProject::instance()->mapLayer( refId ) )
        {
            status.alive = false;
            status.reason = QStringLiteral( "deleted_layer" );
        }
        return status;
    };
    const sicnu::app::MissionReconciliation rec =
        sicnu::app::reconcileMission( state.timeline, resolver );
    if ( !rec.hasIssues() )
        return;
    sicnu::app::applyReconciliation( state.timeline, rec,
                                     QDateTime::currentDateTimeUtc().toString( Qt::ISODate ) );
    QString saveErr;
    QDomDocument doc;
    if ( sicnu::app::saveMissionRuntime( projectPath, doc, state, &saveErr ) )
    {
        m_missionRuntime = state;
        if ( m_missionPanel )
        {
            m_missionPanel->setTimeline( state.timeline );
            m_missionPanel->setMissionHeader(
                state.context.missionId, state.timeline.currentStage(),
                state.timeline.revision(), state.timeline.lastEventSeq() );
        }
        statusBar()->showMessage(
            tr( "Mission: %1 task(s) marked stale after a layer was removed" )
                .arg( rec.staleTaskIds.size() ),
            5000 );
    }
    else
    {
        qWarning( "mission reconcile persist: %s", qPrintable( saveErr ) );
    }
}

void QgisDesktopWindow::syncMissionLayerDisplayNames()
{
    // Layer ids are stable across renames, so the timeline is unaffected;
    // only the mission context's display names need to stay honest.
    bool changed = false;
    for ( sicnu::app::WorkbenchObjectRef &ref : m_mission.layers )
    {
        if ( QgsMapLayer *layer = QgsProject::instance()->mapLayer( ref.id ) )
        {
            if ( ref.displayName != layer->name() )
            {
                ref.displayName = layer->name();
                changed = true;
            }
        }
    }
    if ( changed && m_missionPanel )
        m_missionPanel->setMissionHeader(
            m_mission.missionId, m_missionRuntime.timeline.currentStage(),
            m_missionRuntime.timeline.revision(), m_missionRuntime.timeline.lastEventSeq() );
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
        else if ( !restored.diagnostics().isEmpty() )
        {
            // Non-fatal governance diagnostics (store unavailable/read-only,
            // failed upserts, v1 migration) surface as a status message —
            // they are warnings on a successful read, and silencing them hid
            // partial restores (issue #752).
            statusBar()->showMessage(
                tr( "Project data: %1 governance notice(s) — see logs" )
                    .arg( restored.diagnostics().size() ),
                8000 );
            for ( const sicnu::data::Diagnostic &d : restored.diagnostics() )
                qWarning( "workspace restore notice: %s: %s",
                          qPrintable( d.code ), qPrintable( d.message ) );
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

    // Mission Runtime 13.0: restore the single-authority runtime (context +
    // task-space timeline). A 12.0 timeline sidecar is migrated here; a
    // corrupt authority fails closed without discarding the artifact.
    {
        const QString projectPath = QgsProject::instance()->fileName();
        sicnu::app::MissionRuntimeState runtime;
        QString missionErr;
        if ( sicnu::app::loadMissionRuntime( projectPath, doc, runtime, &missionErr ) )
        {
            m_missionRuntime = runtime;
            m_mission = runtime.context;
            sicnu::app::ensureMissionId( m_mission );
            if ( m_mission.projectRef.isEmpty() && !projectPath.isEmpty() )
                m_mission.projectRef = projectPath;
            m_missionRuntime.context = m_mission;

            // A task left Running by a crashed session must not report
            // Running after the reopen — reconcile against the live
            // execution authorities before any surface reads the timeline.
            const sicnu::app::MissionRunReconciliation runReport =
                sicnu::app::reconcileRunAuthority(
                    m_missionRuntime.timeline, sicnu::app::resolveMissionRunStatus,
                    QDateTime::currentDateTimeUtc().toString( Qt::ISODate ) );
            const bool reconciled = runReport.staleFromRun > 0
                                    || runReport.succeededFromRun > 0
                                    || runReport.failedFromRun > 0
                                    || runReport.canceledFromRun > 0;
            if ( reconciled )
            {
                sicnu::app::MissionRuntimeState persisted = m_missionRuntime;
                QString saveErr;
                QDomDocument saveDoc;
                if ( !sicnu::app::saveMissionRuntime( projectPath, saveDoc, persisted, &saveErr ) )
                    qWarning( "mission runtime reconcile persist: %s", qPrintable( saveErr ) );
                else
                    m_missionRuntime = persisted;
            }

            if ( runtime.authorityLoaded )
                statusBar()->showMessage(
                    tr( "Project loaded · mission %1 restored" )
                        .arg( m_mission.missionId.left( 8 ) ),
                    4000 );
            else
                statusBar()->showMessage( tr( "Project loaded" ), 3000 );
            if ( m_missionPanel )
            {
                m_missionPanel->setTimeline( m_missionRuntime.timeline );
                m_missionPanel->setMissionHeader(
                    m_mission.missionId, m_missionRuntime.timeline.currentStage(),
                    m_missionRuntime.timeline.revision(),
                    m_missionRuntime.timeline.lastEventSeq() );
            }
        }
        else
        {
            // F3: keep the poisoned state so the next save cannot publish an
            // empty mission over the artifact the guard exists to protect.
            m_missionRuntime = runtime;
            m_mission = sicnu::app::MissionContext();
            statusBar()->showMessage(
                tr( "Project loaded · mission runtime unavailable: %1" ).arg( missionErr ), 8000 );
            qWarning( "mission runtime restore: %s", qPrintable( missionErr ) );
        }
    }
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

    // Mission Runtime 13.0: one authority write — the live context plus the
    // task-space timeline embedded in its metadata (sidecar + XML). The live
    // context (studio publishes, active workflow) rides inside the runtime so
    // there is exactly one persisted document. Not inside
    // DataProjectSerializer (governance v3 downgrade risk) — see D-M5.
    {
        const QString projectPath = QgsProject::instance()->fileName();
        if ( m_missionRuntime.authorityCorrupt )
        {
            // The authority could not be decoded at open: publishing anything
            // now would destroy the last known usable artifact. The project
            // itself still saves; only the mission block is refused.
            QMessageBox::warning(
                this, tr( "Mission Context" ),
                tr( "Project saved, but the mission runtime was NOT persisted: its "
                   "authority document could not be read (corrupt or unsupported "
                   "version). Fix or remove the mission sidecar next to the project "
                   "file, then save again." ) );
        }
        else
        {
            // F2: the agent surface commits to the SAME authority between
            // project saves. Reload the timeline here so a save can never
            // revert an agent-committed transition with this window's stale
            // cache. The live context (studio publishes) stays owned here.
            // A successful load can never be poisoned (the store refuses on
            // exactly the paths that set the flag), so a FAILED reload is the
            // poison case: refuse the mission block instead of publishing the
            // window's cache over an artifact that no longer decodes.
            sicnu::app::MissionRuntimeState disk;
            QString reloadErr;
            const bool reloaded =
                sicnu::app::loadMissionRuntime( projectPath, doc, disk, &reloadErr );
            if ( !reloaded || disk.authorityCorrupt )
            {
                QMessageBox::warning(
                    this, tr( "Mission Context" ),
                    tr( "Project saved, but the mission runtime was NOT persisted: "
                       "its authority document could not be read (corrupt or "
                       "unsupported version). Fix or remove the mission sidecar "
                       "next to the project file, then save again." ) );
                qWarning( "mission save refused (authority unreadable): %s",
                          qPrintable( reloadErr ) );
            }
            else
            {
                if ( !disk.timeline.missionId().isEmpty() && !m_mission.missionId.isEmpty()
                     && disk.timeline.missionId() != m_mission.missionId )
                {
                    // Different mission on disk (project switched under us):
                    // keep this window's live mission, do not mix them.
                    qWarning( "mission save: disk mission %s != live mission %s",
                              qPrintable( disk.timeline.missionId() ),
                              qPrintable( m_mission.missionId ) );
                }
                else if ( !disk.authorityLoaded && disk.timeline.missionId().isEmpty() )
                {
                    // No authority at the target path (Save-As to a new path,
                    // moved/copied project directory, or the sidecar removed
                    // per the corrupt-authority dialog's advice): there is
                    // nothing to adopt. Adopting disk's empty timeline would
                    // silently wipe the live task space on the first save —
                    // treat this as first publication of the window's
                    // timeline instead. (A legacy 12.0 timeline sidecar still
                    // adopts: that path yields a non-empty disk mission id.)
                    qWarning( "mission save: no authority at target path, publishing live "
                              "timeline (first publication)" );
                }
                else
                {
                    m_missionRuntime.timeline = disk.timeline;
                }

                if ( m_mission.projectRef.isEmpty() && !projectPath.isEmpty() )
                    m_mission.projectRef = projectPath;
                sicnu::app::ensureMissionId( m_mission );
                m_missionRuntime.context = m_mission;
                QString missionErr;
                if ( !sicnu::app::saveMissionRuntime( projectPath, doc, m_missionRuntime,
                                                      &missionErr ) )
                {
                    QMessageBox::warning(
                        this, tr( "Mission Context" ),
                        tr( "Project saved, but mission runtime persistence failed:\n%1" )
                            .arg( missionErr ) );
                }
                else
                {
                    m_mission = m_missionRuntime.context;
                }
            }
        }
    }
    statusBar()->showMessage(tr("Project saved"), 2000);
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
                      tr( "Display Stretch — %1" ).arg( rl->name() ) );
            }
        }
        syncStatusBarLayer( layer );
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
