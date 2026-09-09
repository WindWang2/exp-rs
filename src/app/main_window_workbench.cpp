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

#include "app/help/help_system_controller.h"
#include "panels/data_manager_panel.h"
#include "panels/workspace_browser_panel.h"
#include "workbench/adapters.h"
#include "workbench/command_defs.h"
#include "workbench/command_palette.h"
#include "workbench/command_registry.h"
#include "workbench/inspector_host.h"
#include "workbench/layer_sections.h"
#include "workbench/processing_history_panel.h"
#include "workbench/provenance_section.h"
#include "workbench/shutdown_policy.h"
#include "workbench/temporal_workbench_panel.h"
#include "workbench/dataset_experiment_panel.h"
#include "workbench/model_workbench_panel.h"
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
#include <QStackedWidget>

#include <qgsproject.h>
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

} // namespace

void QgisDesktopWindow::setupWorkbenchInfrastructure()
{
    m_workbenchHost = new sicnu::app::WorkbenchHost( this );
    m_selectionContext = new sicnu::app::SelectionContext( this );
    m_commandRegistry = new sicnu::app::CommandRegistry( this );

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
            QStringLiteral( "classify" ), tr( "分类工作区" ), QStringLiteral( "su_ervised" ),
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
            QStringLiteral( "georef-i2i" ), tr( "影像对影像配准" ), QStringLiteral( "coregistr_tion" ),
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
            QStringLiteral( "georef-i2m" ), tr( "影像对地图配准" ), QStringLiteral( "geocorrection" ),
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

    // The OBIA window exposes no dirty/in-flight state yet — the bench still
    // gains lifetime tracking and close delegation (#813 baseline).
    {
        auto *bench = new sicnu::app::ExternalWindowWorkbench(
            QStringLiteral( "obia" ), tr( "对象级分类" ), QStringLiteral( "seg_ent_tion" ),
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
        QStringLiteral( "layout" ), tr( "布局设计" ), QStringLiteral( "print_l_yout" ),
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
    m_commandRegistry->setSnapshotProvider( [this] { return m_selectionContext->snapshot(); } );
    registerShellCommands( m_commandRegistry, this );
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
    paletteDef.title = tr( "命令面板..." );
    paletteDef.description = tr( "搜索并执行任意命令（键盘优先）。" );
    paletteDef.iconName = QStringLiteral( "toolbox" );
    paletteDef.shortcut = QKeySequence( QStringLiteral( "Ctrl+Shift+P" ) );
    paletteDef.category = tr( "工具" );
    paletteDef.keywords = { QStringLiteral( "palette" ), QStringLiteral( "命令" ),
                            QStringLiteral( "搜索" ), QStringLiteral( "command" ) };
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
        QMenu *benchMenu = m_windowMenu->addMenu( tr( "工作区" ) );
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
    m_inspectorDock = new QDockWidget( tr( "检查器" ), this );
    m_inspectorDock->setObjectName( QStringLiteral( "rsInspectorDock" ) );
    m_inspectorDock->setAllowedAreas( Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea );
    m_inspectorHost = new sicnu::app::InspectorHost( m_inspectorDock );
    m_inspectorHost->registerSection( new sicnu::app::LayerGeneralSection( m_inspectorHost ) );
    m_inspectorHost->registerSection( new sicnu::app::LayerMetadataSection( m_inspectorHost ) );
    m_inspectorHost->registerSection( new sicnu::app::VectorStructureSection( m_inspectorHost ) );
    m_inspectorHost->registerSection( new sicnu::app::SarInfoSection( m_inspectorHost ) );
    // Workbench 7.0 (goal §B): provenance projection over DataManager +
    // WorkspaceService — services injected, never a copied store.
    m_inspectorHost->registerSection( new sicnu::app::ProvenanceSection(
        [this] -> sicnu::data::DataManager * {
            return m_projectContext ? &m_projectContext->dataManager() : nullptr;
        },
        [this] -> sicnu::workspace::WorkspaceService * {
            return m_projectContext ? &m_projectContext->workspaceService() : nullptr;
        },
        m_inspectorHost ) );
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
    connect( m_historyPanel, &sicnu::app::ProcessingHistoryPanel::resultOpenRequested, this,
             [this]( const QString &path ) { loadRasterLayer( path ); } );
    connect( m_historyPanel, &sicnu::app::ProcessingHistoryPanel::compareRequested, this,
             [this]( const QString &pathA, const QString &pathB ) {
                 // Both artifacts go through the project (the dialog borrows
                 // project-owned layers), then the existing compare surface.
                 if ( !loadRasterLayer( pathA ) || !loadRasterLayer( pathB ) )
                 {
                     statusBar()->showMessage( tr( "无法加载待对比的产物。" ), 4000 );
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
                     statusBar()->showMessage( tr( "未找到待对比的图层。" ), 4000 );
                     return;
                 }
                 ComparisonDialog dialog( this );
                 dialog.setLeftLayer( left );
                 dialog.setRightLayer( right );
                 dialog.exec();
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
             []( const QString &runId ) {
                 QString error;
                 sicnu::workflow::WorkflowRunCoordinator::instance().resumeRun(
                     runId.toStdString(), &error );
                 // Surfacing resume failures happens through the run's own
                 // Failed state in the history — no extra modal surface here.
             } );
    if ( m_windowMenu )
    {
        QAction *historyAction = m_windowMenu->addAction( tr( "处理历史" ) );
        historyAction->setShortcut( QKeySequence( QStringLiteral( "Ctrl+Shift+H" ) ) );
        connect( historyAction, &QAction::triggered, this, [this] {
            m_historyPanel->show();
            m_historyPanel->raise();
            m_historyPanel->activateWindow();
            m_historyPanel->refreshNow();
        } );
    }

    // ── Temporal Workbench (Workbench 7.0 §D) ─────────────────────────
    // Timeline + paginated scene browser over DataManager temporal
    // collections; preview/compare route through the existing seams.
    m_temporalPanel = new sicnu::app::TemporalWorkbenchPanel(
        [this] -> sicnu::data::DataManager * {
            return m_projectContext ? &m_projectContext->dataManager() : nullptr;
        },
        this );
    m_temporalPanel->setObjectName( QStringLiteral( "rsTemporalWorkbenchDock" ) );
    m_temporalPanel->setAllowedAreas( Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea );
    connect( m_temporalPanel, &sicnu::app::TemporalWorkbenchPanel::previewRequested, this,
             [this]( const QString &path ) { loadRasterLayer( path ); } );
    connect( m_temporalPanel, &sicnu::app::TemporalWorkbenchPanel::compareRequested, this,
             [this]( const QString &pathA, const QString &pathB ) {
                 if ( !loadRasterLayer( pathA ) || !loadRasterLayer( pathB ) )
                 {
                     statusBar()->showMessage( tr( "无法加载待对比的时相。" ), 4000 );
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
                     statusBar()->showMessage( tr( "未找到待对比的图层。" ), 4000 );
                     return;
                 }
                 ComparisonDialog dialog( this );
                 dialog.setLeftLayer( left );
                 dialog.setRightLayer( right );
                 dialog.exec();
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
        QAction *temporalAction = m_windowMenu->addAction( tr( "时序工作台" ) );
        temporalAction->setShortcut( QKeySequence( QStringLiteral( "Ctrl+Shift+T" ) ) );
        connect( temporalAction, &QAction::triggered, this, [this] {
            m_temporalPanel->show();
            m_temporalPanel->raise();
            m_temporalPanel->activateWindow();
            m_temporalPanel->refreshCollections();
        } );
    }

    // ── Dataset / Experiment bench (Workbench 7.0 §E) ─────────────────
    // Thin client over the ML-engineering stores (user opens the DB files;
    // the panel projects them read-only, no second store).
    m_datasetExperimentPanel = new sicnu::app::DatasetExperimentPanel( this );
    m_datasetExperimentPanel->setObjectName( QStringLiteral( "rsDatasetExperimentDock" ) );
    m_datasetExperimentPanel->setAllowedAreas( Qt::LeftDockWidgetArea |
                                               Qt::RightDockWidgetArea );
    if ( m_windowMenu )
    {
        QAction *dataAction = m_windowMenu->addAction( tr( "数据集与实验" ) );
        dataAction->setShortcut( QKeySequence( QStringLiteral( "Ctrl+Shift+D" ) ) );
        connect( dataAction, &QAction::triggered, this, [this] {
            m_datasetExperimentPanel->show();
            m_datasetExperimentPanel->raise();
            m_datasetExperimentPanel->activateWindow();
        } );
    }

    // ── Model bench (Workbench 7.0 §F) ────────────────────────────────
    // Catalog/readiness/manifest projection over ModelCatalog + ModelRuntime;
    // test inference submits rs:infer through TaskCenter (goal §F seam).
    m_modelPanel = new sicnu::app::ModelWorkbenchPanel( this );
    m_modelPanel->setObjectName( QStringLiteral( "rsModelWorkbenchDock" ) );
    m_modelPanel->setAllowedAreas( Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea );
    connect( m_modelPanel, &sicnu::app::ModelWorkbenchPanel::inferenceSubmitted, this,
             [this]( long ) {
                 if ( m_historyPanel )
                     m_historyPanel->refreshNow();
             } );
    if ( m_windowMenu )
    {
        QAction *modelAction = m_windowMenu->addAction( tr( "模型工作台" ) );
        modelAction->setShortcut( QKeySequence( QStringLiteral( "Ctrl+Shift+M" ) ) );
        connect( modelAction, &QAction::triggered, this, [this] {
            m_modelPanel->show();
            m_modelPanel->raise();
            m_modelPanel->activateWindow();
            m_modelPanel->refreshCatalog();
        } );
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

    // Stage 1 — in-flight work: the user either cancels it explicitly through
    // the benches'/TaskCenter's own cancel seams, or aborts the operation.
    if ( !plan.inFlightBenches.isEmpty() || plan.runningTaskCount > 0 )
    {
        QString text = tr( "以下工作仍有未完成的任务，%1 会中断它们：\n" ).arg( actionTitle );
        for ( const QString &bench : plan.inFlightBenches )
            text += QStringLiteral( "• 工作区「%1」正在运行任务\n" ).arg( bench );
        if ( plan.runningTaskCount > 0 )
            text += tr( "• 任务中心还有 %1 个未完成任务（含排队/等待资源）\n" ).arg( plan.runningTaskCount );
        text += tr( "\n是否取消这些任务并继续？" );

        QMessageBox box( QMessageBox::Warning, actionTitle, text, QMessageBox::NoButton, this );
        QPushButton *cancelAndContinue =
            box.addButton( tr( "取消任务并继续" ), QMessageBox::AcceptRole );
        QPushButton *stay = box.addButton( tr( "留在当前操作" ), QMessageBox::RejectRole );
        box.setDefaultButton( stay );
        box.exec();
        if ( box.clickedButton() != cancelAndContinue )
            return false;

        // Bounded, cooperative cancel — never waits for terminal state.
        sicnu::app::cancelInFlightBenches( m_workbenchHost );
        for ( long taskId : cancellableTaskIds )
            sicnu::TaskCenter::instance().cancelTask( taskId );
    }

    // Stage 2 — dirty benches: each one runs its own save/discard
    // confirmation via requestClose(); a refusal aborts the operation.
    if ( !plan.dirtyBenches.isEmpty() && !sicnu::app::requestCloseDirtyBenches( m_workbenchHost ) )
        return false;

    return true;
}
