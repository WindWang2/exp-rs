// main_window_view.cpp — Map view and navigation actions
#include "main_window.h"
#include "active_view_host.h"
#include "project_context.h"
#include "shell/view_link_controller.h"
#include "visualanalytics/va_layer_link_controller.h"
#include "shell/rs_session_map_workspace.h"
#include "shell/secondary_map_view_widget.h"
#include "shell/secondary_map_view_session.h"
#include "shell/rs_dual_viewport_sync_controller.h"

#include <QMessageBox>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStatusBar>

#include <qgsmapcanvas.h>
#include <qgsmaplayer.h>
#include <qgsrasterlayer.h>
#include <qgscoordinatetransform.h>
#include <georeferencer/qgsgeoreferencermainwindow.h>
#include <georeferencer/qgsgeoref_image_to_map_window.h>
#include <georeferencer/qgsgeoref_shell_window.h>
#include "workbench/georef_dual_window.h"
#include "workbench/classification_studio_widget.h"
#include "workbench/mission_context.h"
#include "pipeline/ir2_pipeline_designer_dock.h"
#include "workbench/object_identity.h"
#include "workbench/selection_context.h"
#include "workbench/step_explanation_section.h"

#include <QMainWindow>
#include <QVBoxLayout>
#include <QFileInfo>
#include <QJsonObject>
#include <QStatusBar>


#ifdef SICNU_HAS_CLASSIFY
#include "classification/qgsclassificationmainwindow.h"
#endif
#ifdef SICNU_HAS_OBIA
#include "rs_obia_main_window.h"
#endif

// ── View Actions ──────────────────────────────────────────────────────────
void QgisDesktopWindow::zoomIn() { m_mapCanvas->zoomIn(); }
void QgisDesktopWindow::zoomOut() { m_mapCanvas->zoomOut(); }
void QgisDesktopWindow::panMap() { m_mapCanvas->setMapTool(m_panTool); }
void QgisDesktopWindow::identifyFeatures() { m_mapCanvas->setMapTool(m_identifyTool); }

void QgisDesktopWindow::measureDistance()
{
    m_mapCanvas->setMapTool( m_measureDistanceTool );
    statusBar()->showMessage( tr( "Distance measure: click to add points; double-click or right-click to finish" ), 5000 );
}

void QgisDesktopWindow::measureArea()
{
    m_mapCanvas->setMapTool( m_measureAreaTool );
    statusBar()->showMessage( tr( "Area measure: click to add points; double-click or right-click to finish" ), 5000 );
}

void QgisDesktopWindow::openGeoreferencer()
{
    openGeorefImageToImage();
}

namespace
{
/// Register a session map as a secondary Display View (no dual bridges).
bool bindSessionSecondaryView( sicnu::app::ProjectContext *ctx,
                               RsSessionMapWorkspace *session,
                               sicnu::display::DisplayViewId &outId )
{
    if ( !ctx || !session || !outId.isNull() )
        return false;
    session->releaseLocalBridge();
    const auto created = ctx->createSecondaryView( session->viewSpec() );
    if ( !created )
    {
        session->restoreLocalBridge();
        return false;
    }
    outId = created.value();
    return true;
}
} // namespace

void QgisDesktopWindow::registerLinkedVisualView( sicnu::display::DisplayViewId viewId )
{
    if ( viewId.isNull() )
        return;
    if ( m_viewLinkController )
        m_viewLinkController->addView( viewId );
    if ( m_layerLinkController )
        m_layerLinkController->addView( viewId );
}

void QgisDesktopWindow::openGeorefImageToImage()
{
    if ( !m_georefI2I )
    {
        m_georefI2I = new QgsGeoreferencerMainWindow( nullptr, this );
        m_georefI2I->setAttribute( Qt::WA_DeleteOnClose, false );
        m_georefI2I->setWindowTitle( tr( "Image Registration · Image 2 Image" ) );

        connect( m_georefI2I, &QgsGeorefShellWindow::requestLoadToMainMap,
                 this, [this]( const QString &path ) {
                     if ( path.isEmpty() )
                         return;
                     if ( loadDataLayer( path ) )
                         statusBar()->showMessage(
                             tr( "Loaded the correction result into the main view: %1" ).arg( path ), 5000 );
                     else
                         statusBar()->showMessage(
                             tr( "Failed to load the correction result into the main view: %1" ).arg( path ), 6000 );
                 } );

        if ( m_projectContext )
        {
            if ( bindSessionSecondaryView(
                     m_projectContext.get(), m_georefI2I->srcSessionMap(), m_georefI2ISrcViewId ) )
                registerLinkedVisualView( m_georefI2ISrcViewId );
            if ( bindSessionSecondaryView(
                     m_projectContext.get(), m_georefI2I->dstSessionMap(), m_georefI2IDstViewId ) )
                registerLinkedVisualView( m_georefI2IDstViewId );
        }
    }
    m_georefI2I->show();
    m_georefI2I->raise();
    m_georefI2I->activateWindow();
}

void QgisDesktopWindow::openGeorefImageToMap()
{
    if ( !m_georefI2M )
    {
        m_georefI2M = new QgsGeorefImageToMapWindow( nullptr, this );
        m_georefI2M->setAttribute( Qt::WA_DeleteOnClose, false );
        m_georefI2M->setWindowTitle( tr( "Image Registration · Image 2 Map" ) );

        connect( m_georefI2M, &QgsGeorefShellWindow::requestLoadToMainMap,
                 this, [this]( const QString &path ) {
                     if ( path.isEmpty() )
                         return;
                     if ( loadDataLayer( path ) )
                         statusBar()->showMessage(
                             tr( "Loaded the correction result into the main view: %1" ).arg( path ), 5000 );
                     else
                         statusBar()->showMessage(
                             tr( "Failed to load the correction result into the main view: %1" ).arg( path ), 6000 );
                 } );

        if ( m_projectContext )
        {
            if ( bindSessionSecondaryView(
                     m_projectContext.get(), m_georefI2M->srcSessionMap(), m_georefI2MSrcViewId ) )
                registerLinkedVisualView( m_georefI2MSrcViewId );
        }
    }
    m_georefI2M->show();
    m_georefI2M->raise();
    m_georefI2M->activateWindow();
}

#ifdef SICNU_HAS_CLASSIFY
void QgisDesktopWindow::openClassificationWindow()
{
    if ( !m_classifyWindow )
    {
        // iface = nullptr: load-to-main uses requestLoadToMainMap → loadDataLayer.
        m_classifyWindow = new QgsClassificationMainWindow( nullptr, this );
        m_classifyWindow->setAttribute( Qt::WA_DeleteOnClose, false );

        // Inject the shell Data Manager so merged-class outputs are registered
        // as Data Assets (Ticket 02). Other post-process ops are unaffected.
        if ( m_projectContext )
            m_classifyWindow->setDataManager( &m_projectContext->dataManager() );

        connect( m_classifyWindow, &QgsClassificationMainWindow::requestLoadToMainMap,
                 this, [this]( const QString &path ) {
                     if ( path.isEmpty() )
                         return;
                     // D18: path products publish a non-provisional Result into MissionContext.
                     const auto ref = publishStudioResultToMission(
                         path, tr( "Classification product" ) );
                     if ( m_classificationStudio && !ref.isNull() )
                         m_classificationStudio->setMissionResultRef( ref );
                     if ( !ref.isNull() )
                         return; // publishStudioResultToMission already attempted map load
                     if ( loadDataLayer( path ) )
                     {
                         statusBar()->showMessage(
                             tr( "Loaded the classification result into the main view: %1" ).arg( path ), 5000 );
                     }
                     else
                     {
                         statusBar()->showMessage(
                             tr( "Failed to load the classification result into the main view: %1" ).arg( path ), 6000 );
                     }
                 } );

        // Wave E: register session map as secondary Display View (DM owns bridge).
        // #1097: null context must NOT fall into the success branch (would
        // registerLinkedVisualView with a null id).
        if ( m_projectContext )
        {
            if ( !bindSessionSecondaryView( m_projectContext.get(),
                                            m_classifyWindow->sessionMap(),
                                            m_classifyViewId ) )
            {
                statusBar()->showMessage(
                    tr( "Classification session not registered as a display view (using session-local layer stack)" ), 4000 );
            }
            else
            {
                registerLinkedVisualView( m_classifyViewId );
            }
        }
    }
    m_classifyWindow->show();
    m_classifyWindow->raise();
    m_classifyWindow->activateWindow();
}
#else
void QgisDesktopWindow::openClassificationWindow() {
    QMessageBox::information(this, tr("Classification"),
        tr("Supervised classification requires OpenCV with the ml module.\n"
           "Install opencv (including opencv-ml) and rebuild:\n"
           "  cd build && cmake .. && make -j$(nproc)"));
}
#endif

#ifdef SICNU_HAS_OBIA
#include "rs_obia_main_window.h"
void QgisDesktopWindow::openObiaWindow()
{
    if ( !m_obiaWindow )
    {
        auto *obia = new RsObiaMainWindow( this );
        obia->setAttribute( Qt::WA_DeleteOnClose, false );
        // Product UX: load classified result into main project map on request.
        connect( obia, &RsObiaMainWindow::requestLoadToMainMap,
                 this, [this]( const QString &path ) {
                     if ( path.isEmpty() )
                         return;
                     if ( loadDataLayer( path ) )
                     {
                         statusBar()->showMessage(
                             tr( "Loaded the OBIA classification result into the main view: %1" ).arg( path ), 5000 );
                     }
                     else
                     {
                         statusBar()->showMessage(
                             tr( "Failed to load the OBIA result into the main view: %1" ).arg( path ), 6000 );
                     }
                 } );

        // Wave E: register OBIA session map as secondary Display View.
        // #1097: null context must NOT fall into the success branch.
        if ( m_projectContext )
        {
            if ( !bindSessionSecondaryView( m_projectContext.get(),
                                            obia->sessionMap(),
                                            m_obiaViewId ) )
            {
                statusBar()->showMessage(
                    tr( "OBIA session not registered as a display view (using session-local layer stack)" ), 4000 );
            }
            else
            {
                registerLinkedVisualView( m_obiaViewId );
            }
        }
        m_obiaWindow = obia;
    }
    m_obiaWindow->show();
    m_obiaWindow->raise();
    m_obiaWindow->activateWindow();
}
#else
void QgisDesktopWindow::openObiaWindow() {
    QMessageBox::information(this, tr("OBIA"),
        tr("Object-based classification requires OpenCV ml module.\n"
           "Build with SICNU_HAS_OBIA=ON to enable this feature."));
}
#endif


// ── Multi-view shell (Wave D) ─────────────────────────────────────────────

void QgisDesktopWindow::toggleSecondaryMapView( bool on )
{
    if ( on )
        openSecondaryMapView();
    else
        closeSecondaryMapView();
}

void QgisDesktopWindow::openSecondaryMapView()
{
    if ( !m_projectContext || !m_mapSplitter )
        return;

    // The session owns the widget/engine-view/sync state machine (and is
    // the piece the B2 close→reopen fix lives in); the window keeps only
    // the shell-level glue: link registration, active-view fallback and
    // status rendering.
    if ( !m_secondaryMapSession )
    {
        m_secondaryMapSession = new SecondaryMapSession(
            m_mapCanvas, m_mapSplitter, m_projectContext.get(),
            [this]( sicnu::display::DisplayViewId viewId ) {
                registerLinkedVisualView( viewId );
            },
            this );
        m_secondaryMapSession->setActions( m_secondaryViewAction,
                                           m_dualViewportSyncAction );
        connect( m_secondaryMapSession, &SecondaryMapSession::activateRequested,
                 this, &QgisDesktopWindow::activateSecondaryMapView );
        connect( m_secondaryMapSession, &SecondaryMapSession::closeRequested,
                 this, &QgisDesktopWindow::closeSecondaryMapView );
        connect( m_secondaryMapSession, &SecondaryMapSession::syncFromMainRequested,
                 this, &QgisDesktopWindow::syncMainLayersToSecondaryView );
    }

    QString error;
    if ( !m_secondaryMapSession->open( &error ) )
    {
        if ( !error.isEmpty() )
            QMessageBox::warning( this, tr( "Second View" ), error );
        return;
    }
    statusBar()->showMessage( tr( "Second view open. Use 'Active' to switch the display target." ), 4000 );
}

void QgisDesktopWindow::closeSecondaryMapView()
{
    if ( !m_secondaryMapSession )
        return;

    // If secondary was active, fall back to main before teardown.
    if ( m_activeViewHost && m_secondaryMapSession->isOpen()
         && m_activeViewHost->activeViewId() == m_secondaryMapSession->viewId() )
        activateMainMapView();

    m_secondaryMapSession->close();
    statusBar()->showMessage( tr( "Second view closed" ), 2500 );
}

void QgisDesktopWindow::activateMainMapView()
{
    if ( !m_activeViewHost || !m_projectContext )
        return;
    m_activeViewHost->setActiveViewId( m_projectContext->mainViewId() );
    if ( m_secondaryMapSession && m_secondaryMapSession->widget() )
        m_secondaryMapSession->widget()->setActiveHighlight( false );
    statusBar()->showMessage( tr( "Active view: main view" ), 2500 );
}

void QgisDesktopWindow::activateSecondaryMapView()
{
    if ( !m_activeViewHost )
        return;
    if ( !m_secondaryMapSession || m_secondaryMapSession->viewId().isNull() )
    {
        openSecondaryMapView();
        if ( !m_secondaryMapSession || m_secondaryMapSession->viewId().isNull() )
            return;
    }
    if ( !m_activeViewHost->setActiveViewId( m_secondaryMapSession->viewId() ) )
    {
        statusBar()->showMessage( tr( "Cannot activate the second view" ), 3000 );
        return;
    }
    if ( m_secondaryMapSession->widget() )
        m_secondaryMapSession->widget()->setActiveHighlight( true );
    statusBar()->showMessage( tr( "Active view: second view (open / show operations route here)" ), 3500 );
}

void QgisDesktopWindow::syncMainLayersToSecondaryView()
{
    if ( !m_projectContext || !m_secondaryMapSession
         || m_secondaryMapSession->viewId().isNull() )
    {
        statusBar()->showMessage( tr( "Open the second view first" ), 3000 );
        return;
    }

    auto &display = m_projectContext->displayManager();
    const auto mainView = display.view( m_projectContext->mainViewId() );
    if ( !mainView || mainView->layerIds().isEmpty() )
    {
        statusBar()->showMessage( tr( "The main view has no display layers to sync" ), 3000 );
        return;
    }

    int cloned = 0;
    for ( const auto &layerId : mainView->layerIds() )
    {
        const auto result = display.cloneLayer( layerId, m_secondaryMapSession->viewId() );
        if ( result )
            ++cloned;
    }
    statusBar()->showMessage(
        tr( "Cloned %1 main view layers into the second view" ).arg( cloned ), 4000 );
}

void QgisDesktopWindow::toggleDualViewportSync( bool on )
{
    if ( !m_secondaryMapSession || !m_secondaryMapSession->syncController() )
    {
        // No controller yet — keep the action unchecked until the secondary
        // view is opened (which creates the controller).
        if ( m_dualViewportSyncAction )
        {
            QSignalBlocker b( m_dualViewportSyncAction );
            m_dualViewportSyncAction->setChecked( false );
        }
        statusBar()->showMessage( tr( "Open the second view first to enable linked viewports" ), 3000 );
        return;
    }
    m_secondaryMapSession->syncController()->setEnabled( on );
    if ( on )
        m_secondaryMapSession->syncController()->snapSecondaryToPrimary();
    statusBar()->showMessage( on ? tr( "Linked viewports enabled" ) : tr( "Linked viewports paused" ), 2500 );
}

void QgisDesktopWindow::zoomFullExtent()
{
    m_mapCanvas->zoomToFullExtent();
    statusBar()->showMessage(tr("Full Extent"), 2000);
}

void QgisDesktopWindow::zoomToLayer()
{
    QList<QgsMapLayer*> selected = selectedLayers();
    if (!selected.isEmpty()) {
        QgsMapLayer *target = selected.first();
        QgsRectangle extent = target->extent();
        const QgsCoordinateReferenceSystem canvasCrs = m_mapCanvas->mapSettings().destinationCrs();
        if ( target->crs().isValid() && canvasCrs.isValid() && target->crs() != canvasCrs )
        {
            try
            {
                const QgsCoordinateTransform ct( target->crs(), canvasCrs, QgsProject::instance() );
                extent = ct.transformBoundingBox( extent );
            }
            catch ( const QgsCsException & )
            {
                // #1030: fail closed (#1005 sibling). Zooming to a layer extent
                // that is still in the layer CRS jumps the canvas to a
                // meaningless rectangle; refuse instead.
                qWarning().noquote() << "zoomToLayer: CRS transform from"
                                     << target->crs().authid() << "to" << canvasCrs.authid()
                                     << "failed; zoom refused";
                statusBar()->showMessage(
                    tr( "Cannot reproject the layer extent — Zoom to Layer skipped" ), 4000 );
                return;
            }
        }
        m_mapCanvas->setExtent(extent);
        m_mapCanvas->refresh();
        statusBar()->showMessage( tr( "Zoom to Layer" ), 2000 );
    }
}

void QgisDesktopWindow::refreshMap()
{
    m_mapCanvas->refresh();
    statusBar()->showMessage(tr("Canvas refreshed"), 2000);
}


// ── D18 Mission publish + D14/D15/D17 mounts ───────────────────────────────

sicnu::app::WorkbenchObjectRef QgisDesktopWindow::publishStudioResultToMission(
    const QString &path, const QString &displayName )
{
    if ( path.isEmpty() )
        return {};

    const QString name = displayName.isEmpty()
                             ? QFileInfo( path ).fileName()
                             : displayName;
    const auto ref = sicnu::app::publishMissionResultFromPath( m_mission, path, name );

    if ( m_selectionContext && !ref.isNull() )
        m_selectionContext->notifyGovernanceSelection( QStringList{ ref.id } );

    if ( loadDataLayer( path ) )
    {
        statusBar()->showMessage(
            tr( "Published to mission and loaded: %1" ).arg( name ), 5000 );
        // Best-effort: if the active layer is the one we just loaded, record Layer ref.
        if ( m_mapCanvas && m_mapCanvas->currentLayer() )
        {
            sicnu::app::WorkbenchObjectRef layerRef;
            layerRef.kind = sicnu::app::ObjectKind::Layer;
            layerRef.id = m_mapCanvas->currentLayer()->id();
            layerRef.displayName = m_mapCanvas->currentLayer()->name();
            sicnu::app::publishMissionLayer( m_mission, layerRef );
        }
    }
    else
    {
        statusBar()->showMessage(
            tr( "Published to mission (map load failed): %1" ).arg( name ), 6000 );
    }
    return ref;
}

void QgisDesktopWindow::openGeorefDualWindow()
{
    if ( !m_georefDual )
    {
        m_georefDual = new rs::app::GeorefDualWindow( this );
        m_georefDual->setAttribute( Qt::WA_DeleteOnClose, false );
        m_georefDual->setWindowTitle( tr( "Geometric Registration · Dual Window" ) );
        connect( m_georefDual, &rs::app::GeorefDualWindow::rectificationFinished, this,
                 [this]( const QString &path, double finalRmse ) {
                     const auto ref = publishStudioResultToMission(
                         path, tr( "Rectified (RMSE %1)" ).arg( finalRmse, 0, 'f', 3 ) );
                     Q_UNUSED( ref );
                 } );
    }
    m_georefDual->show();
    m_georefDual->raise();
    m_georefDual->activateWindow();
}

void QgisDesktopWindow::openClassificationStudio()
{
    if ( !m_classificationStudioWindow )
    {
        m_classificationStudioWindow = new QMainWindow( this );
        m_classificationStudioWindow->setAttribute( Qt::WA_DeleteOnClose, false );
        m_classificationStudioWindow->setWindowTitle( tr( "Classification / Change Studio" ) );
        m_classificationStudio = new rs::app::ClassificationStudioWidget( m_classificationStudioWindow );
        m_classificationStudioWindow->setCentralWidget( m_classificationStudio );

        connect( m_classificationStudio, &rs::app::ClassificationStudioWidget::classificationRequested,
                 this, [this]( int algoType ) {
                     // Prefer a previously recorded mission product path only
                     // (artifact_paths for an existing Result ref). Never treat the
                     // active map layer as a classification product via name heuristics
                     // — input stacks named *class*/*change*/*predict* must not become Results.
                     QString existingPath;
                     const QJsonObject paths =
                         m_mission.metadata.value( QStringLiteral( "artifact_paths" ) ).toObject();
                     if ( !m_classificationStudio->missionResultRef().isNull() )
                     {
                         const QString rid = m_classificationStudio->missionResultRef().id;
                         if ( paths.contains( rid ) )
                             existingPath = paths.value( rid ).toString();
                     }

                     if ( !existingPath.isEmpty() )
                     {
                         const auto ref = publishStudioResultToMission(
                             existingPath,
                             tr( "Classification product (algo %1)" ).arg( algoType ) );
                         m_classificationStudio->setMissionResultRef( ref );
                         statusBar()->showMessage(
                             tr( "Classification product path published: %1" ).arg( existingPath ),
                             5000 );
                         return;
                     }

                     // Provisional request id — upgraded when classificationProductReady fires.
                     const QString id = QStringLiteral( "classify-studio-%1-%2" )
                                            .arg( algoType )
                                            .arg( m_mission.results.size() );
                     sicnu::app::WorkbenchObjectRef ref;
                     ref.kind = sicnu::app::ObjectKind::Result;
                     ref.id = id;
                     ref.displayName = tr( "Classification request (algo %1)" ).arg( algoType );
                     sicnu::app::publishMissionObject( m_mission, ref );
                     m_classificationStudio->setMissionResultRef( ref );
                     if ( m_selectionContext )
                         m_selectionContext->notifyGovernanceSelection( QStringList{ id } );
                     statusBar()->showMessage(
                         tr( "Classification requested — provisional id %1 (awaiting product path)" )
                             .arg( id ),
                         4000 );
                 } );
        connect( m_classificationStudio, &rs::app::ClassificationStudioWidget::classificationProductReady,
                 this, [this]( const QString &path, int algoType ) {
                     const auto ref = publishStudioResultToMission(
                         path, tr( "Classification product (algo %1)" ).arg( algoType ) );
                     m_classificationStudio->setMissionResultRef( ref );
                 } );
    }

    // Bind mission input from current selection (typed ref; live layer optional).
    if ( m_selectionContext && m_classificationStudio )
    {
        const auto snap = m_selectionContext->snapshot();
        const auto primary = sicnu::app::ContextRules::primaryObject( snap );
        if ( !primary.isNull() )
            m_classificationStudio->setMissionInputRef( primary );
        else if ( snap.activeLayer )
        {
            sicnu::app::WorkbenchObjectRef layerRef;
            layerRef.kind = sicnu::app::ObjectKind::Layer;
            layerRef.id = snap.activeLayer->id();
            layerRef.displayName = snap.activeLayer->name();
            m_classificationStudio->setMissionInputRef( layerRef );
            if ( auto *rl = qobject_cast<QgsRasterLayer *>( snap.activeLayer ) )
                m_classificationStudio->bindInputLayer( rl );
        }
        m_mission = sicnu::app::missionContextFromSelection( snap, m_mission );
        sicnu::app::ensureMissionId( m_mission );
    }

    m_classificationStudioWindow->show();
    m_classificationStudioWindow->raise();
    m_classificationStudioWindow->activateWindow();
}

void QgisDesktopWindow::showIr2PipelineDesigner()
{
    if ( !m_ir2PipelineDock )
    {
        m_ir2PipelineDock = new sicnu::app::pipeline::Ir2PipelineDesignerDock( this );
        addDockWidget( Qt::RightDockWidgetArea, m_ir2PipelineDock );
        connect( m_ir2PipelineDock, &sicnu::app::pipeline::Ir2PipelineDesignerDock::workflowIdentityChanged,
                 this, [this]( const sicnu::app::ActiveWorkflowRef &ref ) {
                     sicnu::app::setMissionActiveWorkflow( m_mission, ref );
                     // RS14-15 R3: a replaced document (New / LabSpec lift)
                     // invalidates the previous run's evidence AND the node
                     // selection — the old run never explains the new
                     // document's nodes.
                     if ( auto *stepSection =
                              m_inspectorHost ? m_inspectorHost->findChild<sicnu::app::StepExplanationSection *>()
                                              : nullptr )
                         stepSection->clearRunEvidence();
                     if ( m_selectionContext )
                         m_selectionContext->notifyPipelineNodeSelection( QString() );
                     statusBar()->showMessage(
                         tr( "Mission workflow identity: %1 (fp %2…)" )
                             .arg( ref.workflowId )
                             .arg( ref.fingerprint.left( 8 ) ),
                         4000 );
                 } );
        connect( m_ir2PipelineDock, &sicnu::app::pipeline::Ir2PipelineDesignerDock::pipelineRunStarted,
                 this, [this]( const QString &runDir ) {
                     statusBar()->showMessage(
                         tr( "PipelineRunCoordinator started: %1" ).arg( runDir ), 5000 );
                 } );
        connect( m_ir2PipelineDock, &sicnu::app::pipeline::Ir2PipelineDesignerDock::pipelineRunFinished,
                 this, [this]( bool success, const QString &summary, const QString &checkpointPath ) {
                     const auto wf = m_ir2PipelineDock->activeWorkflowRef();
                     sicnu::app::setMissionActiveWorkflow( m_mission, wf );
                     sicnu::app::WorkbenchObjectRef runRef;
                     runRef.kind = sicnu::app::ObjectKind::WorkflowRun;
                     runRef.id = QStringLiteral( "ir2-run-%1-%2" )
                                     .arg( wf.workflowId.left( 8 ) )
                                     .arg( m_mission.workflowRuns.size() );
                     runRef.displayName = success ? tr( "IR2 run ok" ) : tr( "IR2 run failed" );
                     sicnu::app::publishMissionObject( m_mission, runRef );
                     if ( !checkpointPath.isEmpty() )
                     {
                         QJsonObject meta = m_mission.metadata;
                         QJsonObject cps = meta.value( QStringLiteral( "ir2_checkpoints" ) ).toObject();
                         cps.insert( runRef.id, checkpointPath );
                         meta.insert( QStringLiteral( "ir2_checkpoints" ), cps );
                         m_mission.metadata = meta;
                     }
                     statusBar()->showMessage(
                         tr( "Pipeline run finished (%1): %2" )
                             .arg( success ? tr( "success" ) : tr( "failed" ), summary ),
                         6000 );
                 } );
        // Explainable Workflow (RS14-15 R3): feed the why-this-step section
        // from the same authoritative seams — canvas node selection into the
        // unified selection context, run lifecycle into the section's
        // run-scoped evidence (loaded once from the finished run's
        // provenance record; a new run clears the previous run's evidence).
        if ( m_selectionContext )
        {
            connect( m_ir2PipelineDock->canvas(),
                     &sicnu::app::pipeline::PipelineCanvasWidget::nodeSelected, m_selectionContext,
                     &sicnu::app::SelectionContext::notifyPipelineNodeSelection );
        }
        if ( m_inspectorHost )
        {
            auto *stepSection = m_inspectorHost->findChild<sicnu::app::StepExplanationSection *>();
            if ( stepSection )
            {
                connect( m_ir2PipelineDock,
                         &sicnu::app::pipeline::Ir2PipelineDesignerDock::pipelineRunStarted,
                         stepSection, [stepSection]( const QString & ) {
                             stepSection->clearRunEvidence();
                         } );
                connect( m_ir2PipelineDock,
                         &sicnu::app::pipeline::Ir2PipelineDesignerDock::pipelineRunFinished,
                         stepSection, [this, stepSection]( bool, const QString &, const QString & ) {
                             stepSection->attachRunProvenance(
                                 m_ir2PipelineDock ? m_ir2PipelineDock->runCoordinator()->provenancePath()
                                                   : QString() );
                         } );
            }
        }
        // Seed mission with the empty document identity immediately.
        sicnu::app::setMissionActiveWorkflow( m_mission, m_ir2PipelineDock->activeWorkflowRef() );
    }
    m_ir2PipelineDock->show();
    m_ir2PipelineDock->raise();
}
