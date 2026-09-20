/***************************************************************************
 * main_window_workbench.cpp — Workbench 5.0 shell wiring
 *
 * Constructs the WorkbenchHost / SelectionContext / CommandRegistry trio,
 * registers the built-in workbenches (map + external session windows) and
 * feeds the registry from the existing window slots. Additive only: session
 * windows keep their own lifecycles; the shell gains a uniform switcher,
 * context projection and command surface above them.
 ***************************************************************************/
#include "main_window.h"

#include <QDateTime>

#include "app/help/help_system_controller.h"
#include "panels/data_manager_panel.h"
#include "panels/workspace_browser_panel.h"
#include "workbench/adapters.h"
#include "workbench/command_defs.h"
#include "workbench/command_palette.h"
#include "workbench/command_registry.h"
#include "workbench/workbench_state.h"
#include "workbench/inspector_host.h"
#include "workbench/layer_sections.h"
#include "workbench/processing_history_panel.h"
#include "workbench/provenance_section.h"
#include "workbench/shutdown_policy.h"
#include "workbench/temporal_workbench_panel.h"
#include "workbench/dataset_experiment_panel.h"
#include "workbench/model_workbench_panel.h"
#include "workbench/object_identity.h"
#include "workbench/mission_context.h"
#include "workbench/mission_runtime_store.h"
#include "workbench/mission_timeline_panel.h"
#include "workbench/mission_tool_host_install.h"
#include "agent/spatial_tools/mission_tools.h"
// F11 unblocking include (pre-existing master break): this TU dereferences
// the rs::app::GeorefDualWindow returned by openGeorefDualWindow() (passes
// it to addDockWidget and reads members) but relied on a transitive include
// that no longer exists after the D14/D17 merges — compiling this TU on
// pristine master fails with incomplete-type errors. See
// .planning/qgis-editing-annotation-11/EVIDENCE.md OUT_OF_SCOPE.
#include "workbench/georef_dual_window.h"
#include "editing/rs_edit_agent_tool.h"
#include "editing/rs_edit_session.h"
#include "editing/rs_snapping_controller.h"

#include <QJsonDocument>
#include <memory>
#include <string>
#include "cartography/cartography_dock.h"
#include "visualanalytics/va_workbench_panel.h"
#include "visualanalytics/va_selection_hub.h"
#include "shell/view_link_controller.h"
#include "visualanalytics/va_layer_link_controller.h"
#include "shell/rs_operator_catalog_panel.h"
#include "shell/workflow_session_controller.h"
#include "workbench/agent_context_tool.h"
#include "project_context.h"
#include "dialogs/comparison_dialog.h"
#include "data/data_asset.h"
#include "data/data_manager.h"
#include "workflow/workflow_run_coordinator.h"
#include "workbench/selection_context.h"
#include "workbench/workbench_host.h"
#include "georeferencer/qgsgeoref_shell_window.h"
#include "georeferencer/qgsgeoreferencermainwindow.h"
#include "georeferencer/qgsgeoref_image_to_map_window.h"
#ifdef SICNU_HAS_CLASSIFY
#include "classification/qgsclassificationmainwindow.h"
#endif

#include <QAction>
#include <QDockWidget>
#include <QMenuBar>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QStatusBar>
#include <QStackedWidget>

#include <qgsmapcanvas.h>
#include <qgsproject.h>
#include <qgspointxy.h>
#include <qgsrasterlayer.h>
#include <qgsmaplayer.h>

#include "processing/framework/task_center.h"

namespace
{

/// Checkable 工作区 switcher section inside the 窗口 menu. Actions track the
/// host's active bench; clicking a bench activates it.
void populateWorkbenchMenu( QMenu *menu, sicnu::app::WorkbenchHost *host )
{
    if ( !menu || !host )
        return;
    menu->clear();
    for ( sicnu::app::IWorkbench *bench : host->workbenches() )
    {
        QAction *action = menu->addAction( bench->icon(), bench->title() );
        action->setCheckable( true );
        action->setChecked( host->isActive( bench->id() ) );
        action->setData( bench->id() );
        QObject::connect( host, &sicnu::app::WorkbenchHost::activeWorkbenchChanged, action,
                 [action, benchId = bench->id()]( const QString &activeId ) {
                     action->setChecked( activeId == benchId );
                 } );
        QObject::connect( action, &QAction::toggled, host, [host, benchId = bench->id(), action]( bool on ) {
            if ( on && !host->isActive( benchId ) )
                host->activate( benchId );
            else if ( !on && host->isActive( benchId ) )
                action->setChecked( true ); // active bench cannot be unchecked
        } );
    }
}

/// Shared compare-surface routing (history panel + temporal bench): load both
/// artifacts through the project (ComparisonDialog borrows project-owned
/// layers), locate them by canonical source and open the existing dialog.
void openComparisonForPaths( QgisDesktopWindow *window, const QString &pathA,
                             const QString &pathB )
{
    if ( !window->loadRasterLayer( pathA ) || !window->loadRasterLayer( pathB ) )
    {
        window->statusBar()->showMessage( QgisDesktopWindow::tr( "Cannot load the artifacts to compare." ), 4000 );
        return;
    }
    QgsRasterLayer *left = nullptr;
    QgsRasterLayer *right = nullptr;
    const QMap<QString, QgsMapLayer *> layers = QgsProject::instance()->mapLayers();
    for ( QgsMapLayer *layer : layers )
    {
        auto *raster = qobject_cast<QgsRasterLayer *>( layer );
        if ( !raster )
            continue;
        if ( raster->source() == pathA )
            left = raster;
        if ( raster->source() == pathB )
            right = raster;
    }
    if ( !left || !right )
    {
        window->statusBar()->showMessage( QgisDesktopWindow::tr( "No layers to compare were found." ), 4000 );
        return;
    }
    ComparisonDialog dialog( window );
    dialog.setLeftLayer( left );
    dialog.setRightLayer( right );
    dialog.exec();
}

} // namespace

void QgisDesktopWindow::setupCommandRegistry()
{
    if ( m_commandRegistry )
        return; // idempotent: menus may request it before the workbench setup
    m_commandRegistry = new sicnu::app::CommandRegistry( this );
    registerShellCommands( m_commandRegistry, this );
}

void QgisDesktopWindow::setupWorkbenchInfrastructure()
{
    m_workbenchHost = new sicnu::app::WorkbenchHost( this );
    m_selectionContext = new sicnu::app::SelectionContext( this );
    // Already created for setupMenu() — keep the same instance and its
    // registered commands (#1031 F-1031-P0-registry).
    setupCommandRegistry();

    // Workbench 8.0: in-flight fact for ContextFacts / suggestedNextAction.
    // The predicate reads TaskCenter's authoritative task set on the GUI
    // thread only (the context recomputes on its own debounce).
    const auto inFlightPredicate = [] {
        const QList<sicnu::AlgorithmTaskInfo> tasks =
            sicnu::TaskCenter::instance().allTasks();
        for ( const sicnu::AlgorithmTaskInfo &task : tasks )
        {
            if ( !sicnu::app::historyTaskTerminal( task.status ) )
                return true;
        }
        return false;
    };
    m_selectionContext->setInFlightTaskPredicate( inFlightPredicate );

    // Workbench 9.0 M1: explicit coarse-state model. Single aggregation
    // point for the empty-state projections (updateCanvasEmptyState /
    // updateLayersEmptyState) and any enable/disable rule derived from the
    // phase — consumers never re-derive hasLayers by hand anymore.
    m_workbenchState = new sicnu::app::WorkbenchStateModel( m_mapCanvas, this );
    m_workbenchState->attachSelectionContext( m_selectionContext );
    m_workbenchState->setInFlightTaskPredicate( inFlightPredicate );
    connect( m_workbenchState, &sicnu::app::WorkbenchStateModel::phaseChanged,
             this, &QgisDesktopWindow::updateCanvasEmptyState );
    connect( m_workbenchState, &sicnu::app::WorkbenchStateModel::phaseChanged,
             this, &QgisDesktopWindow::updateLayersEmptyState );

    // ── Workbenches ───────────────────────────────────────────────────
    m_workbenchHost->registerWorkbench(
        new sicnu::app::MapWorkbench( m_canvasStack, m_workbenchHost ) );

    // External session benches wrap the existing lazy-open slots. No session
    // logic moves here; the benches expose the shared lifecycle contract
    // (#813): window lifetime tracking, dirty state, in-flight compute and
    // cancel routing through each session's own TaskCenter seam, and close
    // delegation to the window's own closeEvent confirmation.
    {
        auto *bench = new sicnu::app::ExternalWindowWorkbench(
            QStringLiteral( "classify" ), tr( "Classification Workspace" ), QStringLiteral( "su_ervised" ),
            [this] {
#ifdef SICNU_HAS_CLASSIFY
                openClassificationWindow();
#endif
            }, m_workbenchHost );
        bench->setWindowGetter( [this]() -> QWidget * {
#ifdef SICNU_HAS_CLASSIFY
            return m_classifyWindow;
#else
            return nullptr;
#endif
        } );
        bench->setDirtyFn( [this] {
#ifdef SICNU_HAS_CLASSIFY
            return m_classifyWindow && m_classifyWindow->isSessionDirty();
#else
            return false;
#endif
        } );
        bench->setInFlightFn( [this] {
#ifdef SICNU_HAS_CLASSIFY
            return m_classifyWindow && m_classifyWindow->hasInFlightCompute();
#else
            return false;
#endif
        } );
        bench->setCancelFn( [this] {
#ifdef SICNU_HAS_CLASSIFY
            if ( !m_classifyWindow || !m_classifyWindow->hasInFlightCompute() )
                return false;
            m_classifyWindow->cancelInFlightCompute();
            return true;
#else
            return false;
#endif
        } );
        bench->setCloseFn( [this] {
#ifdef SICNU_HAS_CLASSIFY
            if ( !m_classifyWindow )
                return true;
            m_classifyWindow->close(); // its closeEvent confirms unsaved state
            return !m_classifyWindow->isVisible();
#else
            return true;
#endif
        } );
        m_workbenchHost->registerWorkbench( bench );
    }

    {
        auto *bench = new sicnu::app::ExternalWindowWorkbench(
            QStringLiteral( "georef-i2i" ), tr( "Image-to-Image Registration" ), QStringLiteral( "coregistr_tion" ),
            [this] { openGeorefImageToImage(); }, m_workbenchHost );
        bench->setWindowGetter( [this]() -> QWidget * { return m_georefI2I; } );
        bench->setDirtyFn( [this] { return m_georefI2I && m_georefI2I->isDirtyForTest(); } );
        bench->setCloseFn( [this] {
            if ( !m_georefI2I )
                return true;
            m_georefI2I->close();
            return !m_georefI2I->isVisible();
        } );
        m_workbenchHost->registerWorkbench( bench );
    }

    {
        auto *bench = new sicnu::app::ExternalWindowWorkbench(
            QStringLiteral( "georef-i2m" ), tr( "Image-to-Map Registration" ), QStringLiteral( "geocorrection" ),
            [this] { openGeorefImageToMap(); }, m_workbenchHost );
        bench->setWindowGetter( [this]() -> QWidget * { return m_georefI2M; } );
        bench->setDirtyFn( [this] { return m_georefI2M && m_georefI2M->isDirtyForTest(); } );
        bench->setCloseFn( [this] {
            if ( !m_georefI2M )
                return true;
            m_georefI2M->close();
            return !m_georefI2M->isVisible();
        } );
        m_workbenchHost->registerWorkbench( bench );
    }

    {
        auto *bench = new sicnu::app::ExternalWindowWorkbench(
            QStringLiteral( "georef-dual" ), tr( "Dual-Window Geometric Registration" ),
            QStringLiteral( "coregistr_tion" ),
            [this] { openGeorefDualWindow(); }, m_workbenchHost );
        bench->setWindowGetter( [this]() -> QWidget * { return m_georefDual; } );
        bench->setCloseFn( [this] {
            if ( !m_georefDual )
                return true;
            m_georefDual->close();
            return !m_georefDual->isVisible();
        } );
        m_workbenchHost->registerWorkbench( bench );
    }

    {
        auto *bench = new sicnu::app::ExternalWindowWorkbench(
            QStringLiteral( "classify-studio" ), tr( "Classification / Change Studio" ),
            QStringLiteral( "su_ervised" ),
            [this] { openClassificationStudio(); }, m_workbenchHost );
        bench->setWindowGetter( [this]() -> QWidget * { return m_classificationStudioWindow; } );
        bench->setCloseFn( [this] {
            if ( !m_classificationStudioWindow )
                return true;
            m_classificationStudioWindow->close();
            return !m_classificationStudioWindow->isVisible();
        } );
        m_workbenchHost->registerWorkbench( bench );
    }

    // The OBIA window exposes no dirty/in-flight state yet — the bench still
    // gains lifetime tracking and close delegation (#813 baseline).
    {
        auto *bench = new sicnu::app::ExternalWindowWorkbench(
            QStringLiteral( "obia" ), tr( "Object-Level Classification" ), QStringLiteral( "seg_ent_tion" ),
            [this] { openObiaWindow(); }, m_workbenchHost );
        bench->setWindowGetter( [this]() -> QWidget * { return m_obiaWindow; } );
        bench->setCloseFn( [this] {
            if ( !m_obiaWindow )
                return true;
            m_obiaWindow->close();
            return !m_obiaWindow->isVisible();
        } );
        m_workbenchHost->registerWorkbench( bench );
    }

    // Layout designers are created per invocation (no singleton window to
    // track); the bench keeps the plain lazy-open contract.
    m_workbenchHost->registerWorkbench( new sicnu::app::ExternalWindowWorkbench(
        QStringLiteral( "layout" ), tr( "Layout Design" ), QStringLiteral( "print_l_yout" ),
        [this] { newLayout(); },
        nullptr,
        nullptr,
        nullptr,
        m_workbenchHost ) );

    m_workbenchHost->activate( QStringLiteral( "map" ) );

    // ── Selection context sources ─────────────────────────────────────
    m_selectionContext->attachCanvas( m_mapCanvas );
    m_selectionContext->attachLayerTree( m_layerTreeView );
    m_selectionContext->attachWorkbenchHost( m_workbenchHost );
    if ( m_dataManagerPanel )
    {
        connect( m_dataManagerPanel, &sicnu::DataManagerPanel::assetSelectionChanged,
                 m_selectionContext, [this]( const QList<sicnu::data::AssetId> &ids ) {
                     QStringList strings;
                     strings.reserve( ids.size() );
                     for ( const auto &id : ids )
                         strings.append( id.toString() );
                     m_selectionContext->notifyAssetSelection( strings );
                 } );
    }
    if ( m_workspaceBrowserPanel )
    {
        connect( m_workspaceBrowserPanel, &sicnu::app::WorkspaceBrowserPanel::entitySelectionChanged,
                 m_selectionContext, [this]( const QStringList &ids ) {
                     m_selectionContext->notifyGovernanceSelection( ids );
                 } );
    }

    // ── Command registry ─────────────────────────────────────────────
    // (the registry itself and the shell commands were created by
    //  setupCommandRegistry() before setupMenu(); only the context binding,
    //  help composition and palette happen here, once the selection context
    //  exists.)
    m_commandRegistry->setSnapshotProvider( [this] { return m_selectionContext->snapshot(); } );
    connect( m_selectionContext, &sicnu::app::SelectionContext::changed, this,
             [this]( const sicnu::app::SelectionContextSnapshot & ) {
                 m_commandRegistry->refreshAll();
             } );

    // ── Unified Help 6.0 ─────────────────────────────────────────────
    // Compose the help knowledge base from the live registries + embedded
    // content, bind command help to every projection action (unavailable
    // commands explain themselves via availability facts), install the F1
    // context filter and track the active workbench for context resolution.
    {
        QStringList compositionErrors;
        sicnu::app::HelpSystemController::instance().compose( *m_commandRegistry,
                                                              &compositionErrors );
        sicnu::app::HelpSystemController::instance().attachCommandRegistry(
            *m_commandRegistry, [this] { return m_selectionContext->snapshot(); } );
        sicnu::app::HelpSystemController::instance().installF1Filter();
        sicnu::app::HelpSystemController::instance().setWorkbenchContext(
            QStringLiteral( "workbench.map" ) );
        connect( m_workbenchHost, &sicnu::app::WorkbenchHost::activeWorkbenchChanged, this,
                 []( const QString &benchId ) {
                     // bench ids may contain '-' (help-id grammar uses '_')
                     sicnu::app::HelpSystemController::instance().setWorkbenchContext(
                         QStringLiteral( "workbench.%1" ).arg( QString( benchId ).replace( u'-', u'_' ) ) );
                 } );
    }

    // ── Command palette (keyboard-first surface, owns no execution) ──
    m_commandPalette = new sicnu::app::CommandPalette( m_commandRegistry, this );
    sicnu::app::CommandDefinition paletteDef;
    paletteDef.id = QStringLiteral( "app.commandPalette" );
    paletteDef.title = tr( "Command Palette..." );
    paletteDef.description = tr( "Search and run any command (keyboard-first)." );
    paletteDef.iconName = QStringLiteral( "toolbox" );
    paletteDef.shortcut = QKeySequence( QStringLiteral( "Ctrl+Shift+P" ) );
    paletteDef.category = tr( "Tools" );
    paletteDef.keywords = { QStringLiteral( "palette" ), tr("Command") ,
                            tr("Search") , QStringLiteral( "command" ) };
    paletteDef.handler = [this] { m_commandPalette->openPalette(); };
    // The palette itself must not appear inside the palette listing.
    if ( m_commandRegistry->registerCommand( paletteDef ) )
    {
        // Canonical shortcut owner: the hidden action-host menubar.
        if ( QAction *paletteAction = m_commandRegistry->action( QStringLiteral( "app.commandPalette" ), true ) )
            appMenuBar()->addAction( paletteAction );
    }

    // ── 窗口 menu → 工作区 switcher ──────────────────────────────────
    if ( m_windowMenu )
    {
        QMenu *benchMenu = m_windowMenu->addMenu( tr( "Workspace" ) );
        benchMenu->setObjectName( QStringLiteral( "rsWorkbenchMenu" ) );
        populateWorkbenchMenu( benchMenu, m_workbenchHost );
        // Late registrations (plugins) extend the switcher.
        connect( m_workbenchHost, &sicnu::app::WorkbenchHost::workbenchRegistered, this,
                 [this]( const QString & ) {
                     if ( QMenu *menu = findChild<QMenu *>( QStringLiteral( "rsWorkbenchMenu" ) ) )
                         populateWorkbenchMenu( menu, m_workbenchHost );
                 } );
    }

    // ── Inspector host dock (right side, follows the selection context) ──
    m_inspectorDock = new QDockWidget( tr( "Inspector" ), this );
    m_inspectorDock->setObjectName( QStringLiteral( "rsInspectorDock" ) );
    m_inspectorDock->setAllowedAreas( Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea );
    m_inspectorHost = new sicnu::app::InspectorHost( m_inspectorDock );
    m_inspectorHost->registerSection( new sicnu::app::LayerGeneralSection( m_inspectorHost ) );
    m_inspectorHost->registerSection( new sicnu::app::LayerMetadataSection( m_inspectorHost ) );
    m_inspectorHost->registerSection( new sicnu::app::VectorStructureSection( m_inspectorHost ) );
    m_inspectorHost->registerSection( new sicnu::app::SarInfoSection( m_inspectorHost ) );
    // Workbench 7.0 (goal §B): provenance projection over DataManager +
    // WorkspaceService — services injected, never a copied store.
    const sicnu::app::ProvenanceSection::DataManagerProvider dmProvider =
        [this]( ) -> sicnu::data::DataManager * {
            return m_projectContext ? &m_projectContext->dataManager() : nullptr;
        };
    const sicnu::app::ProvenanceSection::WorkspaceServiceProvider wsProvider =
        [this]( ) -> sicnu::workspace::WorkspaceService * {
            return m_projectContext ? &m_projectContext->workspaceService() : nullptr;
        };
    m_inspectorHost->registerSection(
        new sicnu::app::ProvenanceSection( dmProvider, wsProvider, m_inspectorHost ) );
    m_inspectorHost->attachSelectionContext( m_selectionContext );
    m_inspectorDock->setWidget( m_inspectorHost );
    addDockWidget( Qt::RightDockWidgetArea, m_inspectorDock );
    m_inspectorDock->hide(); // available on demand — no more permanent chrome

    // ── Processing History (Workbench 7.0 §C) ─────────────────────────
    // Unified projection over TaskCenter + WorkflowRunCoordinator; the panel
    // owns no execution state. Actions route through the same seams.
    m_historyPanel = new sicnu::app::ProcessingHistoryPanel( this );
    m_historyPanel->setObjectName( QStringLiteral( "rsProcessingHistoryDock" ) );
    m_historyPanel->setAllowedAreas( Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea |
                                     Qt::BottomDockWidgetArea );
    addDockWidget( Qt::BottomDockWidgetArea, m_historyPanel );
    m_historyPanel->hide(); // available on demand (窗口 menu / registry command)
    connect( m_historyPanel, &sicnu::app::ProcessingHistoryPanel::resultOpenRequested, this,
             [this]( const QString &path ) {
                 if ( !loadRasterLayer( path ) )
                     statusBar()->showMessage( tr( "Cannot open the artifacts on the map: %1" ).arg( path ), 5000 );
             } );
    connect( m_historyPanel, &sicnu::app::ProcessingHistoryPanel::compareRequested, this,
             [this]( const QString &pathA, const QString &pathB ) {
                 openComparisonForPaths( this, pathA, pathB );
             } );
    connect( m_historyPanel, &sicnu::app::ProcessingHistoryPanel::inspectRequested, this,
             [this]( const QString &path ) {
                 if ( !m_projectContext )
                     return;
                 // Resolve the artifact to its registered asset and steer the
                 // shared selection context: the provenance inspector section
                 // renders whatever the catalog truly knows about it.
                 const std::optional<sicnu::data::AssetSnapshot> asset =
                     m_projectContext->dataManager().findByPath( path );
                 if ( asset )
                     m_selectionContext->notifyAssetSelection(
                         QStringList{ asset->id().toString() } );
             } );
    connect( m_historyPanel, &sicnu::app::ProcessingHistoryPanel::resumeRunRequested, this,
             [this]( const QString &runId ) {
                 QString error;
                 const long taskId = sicnu::workflow::WorkflowRunCoordinator::instance().resumeRun(
                     runId.toStdString(), &error );
                 // resumeRun rejects WITHOUT changing run state (lock held,
                 // checkpoint missing/corrupt) — that must surface now, not
                 // "later through some state change that never comes".
                 if ( taskId < 0 )
                     statusBar()->showMessage(
                         tr( "Failed to resume run %1: %2" )
                             .arg( runId, error.isEmpty() ? tr( "Unknown reason" ) : error ),
                         6000 );
                 if ( m_historyPanel )
                     m_historyPanel->refreshNow();
             } );
    if ( m_windowMenu )
    {
        if ( QAction *action = m_commandRegistry->action( QStringLiteral( "workbench.processingHistory" ), true ) )
            m_windowMenu->addAction( action );
    }

    // ── Mission Runtime 13.0 — mission task space surface ─────────────
    // One host wiring for both execution modes of this binary (the headless
    // --mcp branch calls the same installer from main.cpp).
    sicnu::app::installMissionToolHost();

    m_missionPanel = new sicnu::app::MissionTimelinePanel( this );
    m_missionPanel->setObjectName( QStringLiteral( "missionTimelineDock" ) );
    m_missionPanel->setAllowedAreas( Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea |
                                     Qt::BottomDockWidgetArea );
    addDockWidget( Qt::BottomDockWidgetArea, m_missionPanel );
    m_missionPanel->hide(); // available on demand (窗口 menu / registry command)
    connect( m_missionPanel, &sicnu::app::MissionTimelinePanel::taskSelected, this,
             [this]( const QString &taskId, sicnu::app::MissionTaskStatus status ) {
                 if ( m_selectionContext )
                     m_selectionContext->notifyMissionTaskSelection( taskId, status );
             } );
    connect( m_missionPanel, &sicnu::app::MissionTimelinePanel::refreshRequested, this,
             &QgisDesktopWindow::refreshMissionRuntime );
    connect( m_missionPanel, &sicnu::app::MissionTimelinePanel::retryRequested, this,
             [this]( const QString & ) { retrySelectedMissionTask(); } );
    connect( m_missionPanel, &sicnu::app::MissionTimelinePanel::resumeRequested, this,
             [this]( const QString & ) { resumeSelectedMissionTask(); } );
    if ( m_windowMenu )
    {
        if ( QAction *action = m_commandRegistry->action( QStringLiteral( "mission.timeline.show" ), true ) )
            m_windowMenu->addAction( action );
    }

    // ── Temporal Workbench (Workbench 7.0 §D) ─────────────────────────
    // Timeline + paginated scene browser over DataManager temporal
    // collections; preview/compare route through the existing seams.
    const sicnu::app::TemporalWorkbenchPanel::DataManagerProvider temporalProvider =
        [this]( ) -> sicnu::data::DataManager * {
            return m_projectContext ? &m_projectContext->dataManager() : nullptr;
        };
    m_temporalPanel = new sicnu::app::TemporalWorkbenchPanel( temporalProvider, this );
    m_temporalPanel->setObjectName( QStringLiteral( "rsTemporalWorkbenchDock" ) );
    m_temporalPanel->setAllowedAreas( Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea );
    addDockWidget( Qt::RightDockWidgetArea, m_temporalPanel );
    m_temporalPanel->hide();
    connect( m_temporalPanel, &sicnu::app::TemporalWorkbenchPanel::previewRequested, this,
             [this]( const QString &path ) { loadRasterLayer( path ); } );
    connect( m_temporalPanel, &sicnu::app::TemporalWorkbenchPanel::compareRequested, this,
             [this]( const QString &pathA, const QString &pathB ) {
                 openComparisonForPaths( this, pathA, pathB );
             } );
    // Collections change with the project data context; the panel re-reads.
    if ( m_projectContext )
    {
        connect( &m_projectContext->dataManager(), &sicnu::data::DataManager::temporalCollectionAdded,
                 m_temporalPanel, &sicnu::app::TemporalWorkbenchPanel::refreshCollections );
        connect( &m_projectContext->dataManager(),
                 &sicnu::data::DataManager::temporalCollectionChanged,
                 m_temporalPanel, &sicnu::app::TemporalWorkbenchPanel::refreshCollections );
        connect( &m_projectContext->dataManager(),
                 &sicnu::data::DataManager::temporalCollectionRemoved,
                 m_temporalPanel, &sicnu::app::TemporalWorkbenchPanel::refreshCollections );
    }
    if ( m_windowMenu )
    {
        if ( QAction *action = m_commandRegistry->action( QStringLiteral( "workbench.temporal" ), true ) )
            m_windowMenu->addAction( action );
    }

    // ── Dataset / Experiment bench (Workbench 7.0 §E) ─────────────────
    // Thin client over the ML-engineering stores (user opens the DB files;
    // the panel projects them read-only, no second store).
    m_datasetExperimentPanel = new sicnu::app::DatasetExperimentPanel( this );
    m_datasetExperimentPanel->setObjectName( QStringLiteral( "rsDatasetExperimentDock" ) );
    m_datasetExperimentPanel->setAllowedAreas( Qt::LeftDockWidgetArea |
                                               Qt::RightDockWidgetArea );
    addDockWidget( Qt::RightDockWidgetArea, m_datasetExperimentPanel );
    m_datasetExperimentPanel->hide();
    if ( m_windowMenu )
    {
        if ( QAction *action = m_commandRegistry->action( QStringLiteral( "workbench.datasetExperiment" ), true ) )
            m_windowMenu->addAction( action );
    }

    // ── Model bench (Workbench 7.0 §F) ────────────────────────────────
    // Catalog/readiness/manifest projection over ModelCatalog + ModelRuntime;
    // test inference submits rs:infer through TaskCenter (goal §F seam).
    m_modelPanel = new sicnu::app::ModelWorkbenchPanel( this );
    m_modelPanel->setObjectName( QStringLiteral( "rsModelWorkbenchDock" ) );
    m_modelPanel->setAllowedAreas( Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea );
    addDockWidget( Qt::RightDockWidgetArea, m_modelPanel );
    m_modelPanel->hide();
    connect( m_modelPanel, &sicnu::app::ModelWorkbenchPanel::inferenceSubmitted, this,
             [this]( long ) {
                 if ( m_historyPanel )
                     m_historyPanel->refreshNow();
             } );
    if ( m_windowMenu )
    {
        if ( QAction *action = m_commandRegistry->action( QStringLiteral( "workbench.model" ), true ) )
            m_windowMenu->addAction( action );
    }

    // ── Unified object selection (Workbench 10.0) ─────────────────────
    // The dataset/experiment, model and history panels join the layer/data/
    // governance sources: every selection kind lands in ONE SelectionContext.
    connect( m_datasetExperimentPanel,
             &sicnu::app::DatasetExperimentPanel::experimentRunSelectionChanged,
             m_selectionContext, &sicnu::app::SelectionContext::notifyExperimentSelection );
    connect( m_datasetExperimentPanel,
             &sicnu::app::DatasetExperimentPanel::datasetSelectionChanged, m_selectionContext,
             [this]( const QString &datasetId ) {
                 m_selectionContext->notifyDatasetSelection(
                     datasetId.isEmpty() ? QStringList() : QStringList{ datasetId } );
             } );
    connect( m_modelPanel, &sicnu::app::ModelWorkbenchPanel::modelSelectionChanged,
             m_selectionContext, &sicnu::app::SelectionContext::notifyModelSelection );
    connect( m_historyPanel, &sicnu::app::ProcessingHistoryPanel::workflowRunSelectionChanged,
             m_selectionContext, &sicnu::app::SelectionContext::notifyWorkflowSelection );

    // ── UI→agent context projection (Workbench 10.0, read-only) ───────
    // Registers the `workbench:context` spatial tool: the agent can read the
    // live selection/context; writes keep flowing through the existing
    // command/tool authority. The provider re-derives its target through a
    // QPointer on EVERY call: SpatialToolRegistry keeps the FIRST
    // registration for a name, so a stale lambda would be a UAF trap if it
    // captured raw `this` across any hypothetical in-process re-assembly.
    {
        QPointer<QgisDesktopWindow> self( this );
        auto *contextTool = new sicnu::app::WorkbenchContextTool(
            [self]() -> Json::Value {
                if ( !self || !self->m_selectionContext || !self->m_commandRegistry )
                    return Json::Value();
                const auto snap = self->m_selectionContext->snapshot();
                Json::Value payload = sicnu::app::workbenchContextToJson(
                    snap, self->m_commandRegistry->commandIds() );
                // D18: bounded mission summary for Agent grounding (GOAL §9).
                // Prefer the live session mission (studio publishes + IR2 identity),
                // then overlay the current selection projection.
                sicnu::app::MissionContext mission =
                    sicnu::app::missionContextFromSelection( snap, self->m_mission );
                if ( self->m_temporalPanel )
                    mission.temporal = self->m_temporalPanel->exportTemporalContext();
                if ( !self->m_mission.activeWorkflow.isNull() )
                    mission.activeWorkflow = self->m_mission.activeWorkflow;
                self->m_mission = mission;
                sicnu::app::ensureMissionId( self->m_mission );
                mission = self->m_mission;
                const QJsonObject summary = sicnu::app::missionSummaryJson( mission );
                const QByteArray bytes =
                    QJsonDocument( summary ).toJson( QJsonDocument::Compact );
                Json::Value missionJson;
                Json::CharReaderBuilder builder;
                std::string errs;
                const std::unique_ptr<Json::CharReader> reader( builder.newCharReader() );
                if ( reader->parse( bytes.constData(), bytes.constData() + bytes.size(),
                                    &missionJson, &errs ) )
                {
                    payload["mission"] = missionJson;
                }
                return payload;
            } );
        sicnu::agent::spatial_tools::SpatialToolRegistry::instance().registerTool(
            sicnu::agent::spatial_tools::SpatialToolPtr{ contextTool } );
    }

    // ── Editing platform 11.0 (F11, read-only agent surface) ──────────
    // One edit session per window, parented to it; the `editing:state`
    // spatial tool exposes session/snapping FACTS to the agent. No write
    // path is exposed here — layer writes keep flowing through the
    // existing map tools and their undo stacks.
    {
        auto *editSession = new RsEditSession( this );
        auto *snappingController = new RsSnappingController( m_mapCanvas, editSession );
        RsEditAgentTool::Sources sources;
        sources.session = editSession;
        sources.snapping = snappingController;
        auto *editStateTool = new RsEditAgentTool( sources );
        sicnu::agent::spatial_tools::SpatialToolRegistry::instance().registerTool(
          sicnu::agent::spatial_tools::SpatialToolPtr{ editStateTool } );
    }

    // ── Cartography bridge (Workbench 10.0, C-1) ──────────────────────
    // Desktop surface over the SAME cartography:* operator family workflow
    // nodes dispatch; inputs seed from the unified selection's map layers.
    m_cartographyDock = new sicnu::app::CartographyDock(
        [this]() -> QStringList {
            QStringList sources;
            if ( !m_selectionContext )
                return sources;
            const auto snap = m_selectionContext->snapshot();
            const auto addLayer = [&sources]( QgsMapLayer *layer ) {
                if ( layer && layer->isValid() && !sources.contains( layer->source() ) )
                    sources.append( layer->source() );
            };
            // Selection first (primary object's provenance feeds the map),
            // then the rest of the canvas so a template always has content.
            addLayer( snap.activeLayer );
            for ( QgsMapLayer *layer : snap.selectedLayers )
                addLayer( layer );
            return sources;
        },
        [this]() -> QString {
            // Default export directory: the current project home (empty when
            // no project — the file dialog then falls back to the cwd).
            return QgsProject::instance()->homePath();
        },
        this );
    m_cartographyDock->setAllowedAreas( Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea );
    addDockWidget( Qt::RightDockWidgetArea, m_cartographyDock );
    m_cartographyDock->hide();
    connect( m_cartographyDock, &sicnu::app::CartographyDock::statusMessage, this,
             [this]( const QString &message ) { statusBar()->showMessage( message, 8000 ); } );
    if ( m_windowMenu )
    {
        if ( QAction *action = m_commandRegistry->action( QStringLiteral( "workbench.cartography" ), true ) )
            m_windowMenu->addAction( action );
    }

    // ── Linked Visual Analytics 11.0 ──────────────────────────────────
    // Process-level selection hub + N-view extent/cursor link + cross-view
    // layer visibility/opacity link, all over the display manager's view
    // authority. The main view registers here; secondary/session views
    // register where they are created (openSecondaryMapView & co).
    if ( m_projectContext )
    {
        m_vaSelectionHub = new sicnu::app::va::VaSelectionHub( this );
        m_viewLinkController = new sicnu::app::ViewLinkController(
            &m_projectContext->displayManager(), this );
        m_layerLinkController = new sicnu::app::VaLayerLinkController(
            &m_projectContext->displayManager(), this );
        const auto mainViewId = m_projectContext->mainViewId();
        if ( !mainViewId.isNull() )
        {
            m_viewLinkController->addView( mainViewId );
            m_layerLinkController->addView( mainViewId );
        }
    }

    // ── Visual Analytics workbench (Workbench 10.0) ───────────────────
    // Typed chart hosts over bounded, cancellable sampling jobs; inputs
    // follow the unified selection's raster. 11.0: the panel joins the
    // selection hub (brushing) and the view link's cursor channel.
    m_vaPanel = new sicnu::app::va::VaWorkbenchPanel(
        [this]() -> QString {
            if ( !m_selectionContext )
                return QString();
            const auto snap = m_selectionContext->snapshot();
            if ( const QgsRasterLayer *raster = snap.firstRasterLayer() )
                return raster->source();
            return QString();
        },
        m_vaSelectionHub,
        [this]() -> QgsMapCanvas * {
            // The pick marker belongs on the view the user is actually
            // looking at (display-manager active view), not always the main.
            if ( !m_projectContext )
                return nullptr;
            auto &display = m_projectContext->displayManager();
            return display.mapCanvas( display.activeViewId() );
        },
        [this]() -> QgsRasterLayer * {
            if ( !m_selectionContext )
                return nullptr;
            return m_selectionContext->snapshot().firstRasterLayer();
        },
        this );
    if ( m_viewLinkController && m_vaPanel )
    {
        connect( m_viewLinkController, &sicnu::app::ViewLinkController::cursorMoved,
                 m_vaPanel,
                 [this]( sicnu::display::DisplayViewId viewId, const QgsPointXY &point,
                         const QString &crsWkt ) {
                     if ( m_vaPanel )
                         m_vaPanel->onViewCursorMoved( viewId.toString(), point.x(),
                                                       point.y(), crsWkt );
                 } );
        connect( m_viewLinkController, &sicnu::app::ViewLinkController::cursorLeft,
                 m_vaPanel, [this]( sicnu::display::DisplayViewId viewId ) {
                     if ( m_vaPanel )
                         m_vaPanel->onViewCursorLeft( viewId.toString() );
                 } );
    }
    m_vaPanel->setAllowedAreas( Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea );
    addDockWidget( Qt::RightDockWidgetArea, m_vaPanel );
    m_vaPanel->hide();
    if ( m_windowMenu )
    {
        if ( QAction *action = m_commandRegistry->action( QStringLiteral( "workbench.visualAnalytics" ), true ) )
            m_windowMenu->addAction( action );
    }

    // ── rs: operator catalog (Workbench 10.0, WP-F) ───────────────────
    // Search/recent/favorites over the operator registry; opening an entry
    // rides the workflow session (TaskCenter stays the only executor).
    m_operatorCatalogPanel = new sicnu::app::RsOperatorCatalogPanel( this );
    m_operatorCatalogPanel->setAllowedAreas( Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea );
    addDockWidget( Qt::LeftDockWidgetArea, m_operatorCatalogPanel );
    m_operatorCatalogPanel->hide();
    // NOTE: the session controller is created AFTER this wiring point
    // (setupRibbonAndTaskPanel) — the check must live inside the handler,
    // never around the connect, or the catalog's open path stays dead.
    connect( m_operatorCatalogPanel, &sicnu::app::RsOperatorCatalogPanel::operatorSelected,
             this, [this]( const QString &operatorId ) {
                 if ( !m_sessionController )
                     return;
                 m_sessionController->openBareOperator( operatorId );
                 m_operatorCatalogPanel->noteOperatorRun( operatorId );
             } );
    if ( m_windowMenu )
    {
        if ( QAction *action = m_commandRegistry->action( QStringLiteral( "workbench.operatorCatalog" ), true ) )
            m_windowMenu->addAction( action );
    }
}

bool QgisDesktopWindow::confirmWorkbenchShutdown( const QString &actionTitle )
{
    // Non-terminal TaskCenter tasks: every surface (GUI, agent, workflow,
    // CLI-enqueued) converges here, so the count is the truthful "work in
    // progress" projection (goal §A: no silent drop).
    int runningTaskCount = 0;
    QList<long> cancellableTaskIds;
    const QList<sicnu::AlgorithmTaskInfo> tasks = sicnu::TaskCenter::instance().allTasks();
    for ( const sicnu::AlgorithmTaskInfo &task : tasks )
    {
        switch ( task.status )
        {
            case sicnu::TaskStatus::Queued:
            case sicnu::TaskStatus::Running:
            case sicnu::TaskStatus::Paused:
            case sicnu::TaskStatus::WaitingResource:
            case sicnu::TaskStatus::Dispatching:
            case sicnu::TaskStatus::Cancelling: // cancel in flight — still in-flight work (#A)
                ++runningTaskCount;
                cancellableTaskIds.append( task.taskId );
                break;
            case sicnu::TaskStatus::Completed:
            case sicnu::TaskStatus::Failed:
            case sicnu::TaskStatus::Canceled:
                break;
        }
    }

    const sicnu::app::ShutdownPlan plan = sicnu::app::planWorkbenchShutdown(
        sicnu::app::collectWorkbenchShutdownFacts( m_workbenchHost ), runningTaskCount );
    if ( plan.isEmpty() )
        return true;

    // Stage 1 — dirty benches FIRST: their requestClose() confirmations can
    // still abort the whole operation, and an abort must not leave already
    // cancelled compute behind (review M8: cancel-before-confirm order).
    if ( !plan.dirtyBenches.isEmpty() && !sicnu::app::requestCloseDirtyBenches( m_workbenchHost ) )
        return false;

    // Stage 2 — in-flight work: the user either cancels it explicitly through
    // the benches'/TaskCenter's own cancel seams, or aborts the operation.
    if ( !plan.inFlightBenches.isEmpty() || plan.runningTaskCount > 0 )
    {
        QString text = tr( "The following jobs still have unfinished tasks; %1 will interrupt them:\n" ).arg( actionTitle );
        for ( const QString &bench : plan.inFlightBenches )
            text += tr("• Workspace '%1' has tasks running\n" ).arg( bench );
        if ( plan.runningTaskCount > 0 )
            text += tr( "• The Task Center still has %1 unfinished tasks (queued / waiting for resources)\n" ).arg( plan.runningTaskCount );
        text += tr( "\nCancel these tasks and continue?" );

        QMessageBox box( QMessageBox::Warning, actionTitle, text, QMessageBox::NoButton, this );
        QPushButton *cancelAndContinue =
            box.addButton( tr( "Cancel Task and Continue" ), QMessageBox::AcceptRole );
        QPushButton *stay = box.addButton( tr( "Stay on Current Operation" ), QMessageBox::RejectRole );
        box.setDefaultButton( stay );
        box.exec();
        if ( box.clickedButton() != cancelAndContinue )
            return false;

        // Bounded, cooperative cancel — never waits for terminal state.
        sicnu::app::cancelInFlightBenches( m_workbenchHost );
        for ( long taskId : cancellableTaskIds )
            sicnu::TaskCenter::instance().cancelTask( taskId );
    }

    return true;
}

void QgisDesktopWindow::showUnifiedProcessingHistory()
{
    if ( !m_historyPanel )
        return;
    m_historyPanel->show();
    m_historyPanel->raise();
    m_historyPanel->activateWindow();
    m_historyPanel->refreshNow();
}

void QgisDesktopWindow::showTemporalWorkbench()
{
    if ( !m_temporalPanel )
        return;
    m_temporalPanel->show();
    m_temporalPanel->raise();
    m_temporalPanel->activateWindow();
    m_temporalPanel->refreshCollections();
}

void QgisDesktopWindow::showDatasetExperimentBench()
{
    if ( !m_datasetExperimentPanel )
        return;
    m_datasetExperimentPanel->show();
    m_datasetExperimentPanel->raise();
    m_datasetExperimentPanel->activateWindow();
}

void QgisDesktopWindow::showModelBench()
{
    if ( !m_modelPanel )
        return;
    m_modelPanel->show();
    m_modelPanel->raise();
    m_modelPanel->activateWindow();
    m_modelPanel->refreshCatalog();
}

void QgisDesktopWindow::showCartographyDock()
{
    if ( !m_cartographyDock )
        return;
    m_cartographyDock->show();
    m_cartographyDock->raise();
    m_cartographyDock->activateWindow();
}

void QgisDesktopWindow::showVisualAnalyticsPanel()
{
    if ( !m_vaPanel )
        return;
    m_vaPanel->show();
    m_vaPanel->raise();
    m_vaPanel->activateWindow();
    m_vaPanel->refreshCharts();
}

void QgisDesktopWindow::showOperatorCatalog()
{
    if ( !m_operatorCatalogPanel )
        return;
    m_operatorCatalogPanel->show();
    m_operatorCatalogPanel->raise();
    m_operatorCatalogPanel->activateWindow();
}

// ── Mission Runtime 13.0 — mission task space surface ─────────────────────

void QgisDesktopWindow::refreshMissionRuntime()
{
    const QString projectPath = QgsProject::instance()->fileName();
    sicnu::app::MissionRuntimeState state;
    QString err;
    if ( !sicnu::app::loadMissionRuntime( projectPath, QDomDocument(), state, &err ) )
    {
        // Fail closed and explain: a corrupt authority must never be
        // silently replaced by an empty task space.
        statusBar()->showMessage( tr( "Mission runtime unavailable: %1" ).arg( err ), 8000 );
        if ( m_missionPanel )
        {
            m_missionPanel->setTimeline( sicnu::app::MissionTimeline() );
            m_missionPanel->setMissionHeader( QString(), sicnu::app::MissionStage::Import, 0, 0 );
        }
        return;
    }

    for ( const QString &notice : state.notices )
    {
        if ( notice == QLatin1String( "timeline_migrated_from_legacy_sidecar" ) )
            statusBar()->showMessage( tr( "Mission timeline migrated from the 12.0 sidecar" ),
                                      6000 );
        else if ( notice == QLatin1String( "authority_recovered_from_last_good" ) )
            statusBar()->showMessage(
                tr( "Mission context recovered from the last known good state" ), 8000 );
    }

    // A reopened project must not report a task as Running whose execution
    // no longer exists (crash residue) — reconcile before any surface reads.
    const sicnu::app::MissionRunReconciliation runReport = sicnu::app::reconcileRunAuthority(
        state.timeline, sicnu::app::resolveMissionRunStatus,
        QDateTime::currentDateTimeUtc().toString( Qt::ISODate ) );
    if ( runReport.staleFromRun > 0 || runReport.succeededFromRun > 0 || runReport.failedFromRun > 0
         || runReport.canceledFromRun > 0 )
    {
        // Persist the reconciled truth so every surface (and the next open)
        // agrees.
        sicnu::app::MissionRuntimeState persisted = state;
        QString saveErr;
        QDomDocument doc;
        sicnu::app::saveMissionRuntime( projectPath, doc, persisted, &saveErr );
        state = persisted;
    }

    m_missionRuntime = state;
    m_mission = state.context;
    if ( m_missionPanel )
    {
        m_missionPanel->setTimeline( state.timeline );
        m_missionPanel->setMissionHeader( state.context.missionId, state.timeline.currentStage(),
                                          state.timeline.revision(),
                                          state.timeline.lastEventSeq() );
    }
}

void QgisDesktopWindow::showMissionTimelinePanel()
{
    if ( !m_missionPanel )
        return;
    refreshMissionRuntime();
    m_missionPanel->show();
    m_missionPanel->raise();
    m_missionPanel->activateWindow();
}

void QgisDesktopWindow::retrySelectedMissionTask()
{
    if ( !m_selectionContext )
        return;
    const auto snap = m_selectionContext->snapshot();
    if ( !snap.hasMissionTaskSelection )
    {
        statusBar()->showMessage( tr( "Select a mission task first" ), 4000 );
        return;
    }
    const sicnu::agent::spatial_tools::MissionActionResult result = sicnu::agent::spatial_tools::
        applyMissionAction( snap.selectedMissionTaskId, QStringLiteral( "retry" ) );
    if ( result.transportFailure )
    {
        statusBar()->showMessage(
            tr( "Mission task retry failed: %1" )
                .arg( result.errorMessage.isEmpty() ? result.errorCode : result.errorMessage ),
            6000 );
        return;
    }
    statusBar()->showMessage(
        result.applied ? tr( "Mission task requeued" )
                       : tr( "Mission task not retryable: %1" ).arg( result.reason ),
        4000 );
    refreshMissionRuntime();
}

void QgisDesktopWindow::resumeSelectedMissionTask()
{
    if ( !m_selectionContext )
        return;
    const auto snap = m_selectionContext->snapshot();
    if ( !snap.hasMissionTaskSelection )
    {
        statusBar()->showMessage( tr( "Select a mission task first" ), 4000 );
        return;
    }
    // Resume = re-bind the references first (a stale task may reference
    // layers that came back), then requeue — the same two actions the agent
    // surface exposes, in the same order.
    const sicnu::agent::spatial_tools::MissionActionResult reconciled = sicnu::agent::spatial_tools::
        applyMissionAction( snap.selectedMissionTaskId, QStringLiteral( "reconcile" ) );
    const sicnu::agent::spatial_tools::MissionActionResult result = sicnu::agent::spatial_tools::
        applyMissionAction( snap.selectedMissionTaskId, QStringLiteral( "retry" ) );
    if ( result.transportFailure || reconciled.transportFailure )
    {
        statusBar()->showMessage(
            tr( "Mission task resume failed: %1" )
                .arg( ( result.transportFailure ? result.errorMessage : reconciled.errorMessage )
                          .isEmpty()
                          ? result.errorCode
                          : ( result.transportFailure ? result.errorMessage
                                                      : reconciled.errorMessage ) ),
            6000 );
        return;
    }
    statusBar()->showMessage(
        result.applied ? tr( "Mission task resumed" )
                       : tr( "Mission task not resumable: %1" ).arg( result.reason ),
        4000 );
    refreshMissionRuntime();
}
