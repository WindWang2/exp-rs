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

#include "panels/data_manager_panel.h"
#include "panels/workspace_browser_panel.h"
#include "workbench/adapters.h"
#include "workbench/command_defs.h"
#include "workbench/command_palette.h"
#include "workbench/command_registry.h"
#include "workbench/inspector_host.h"
#include "workbench/layer_sections.h"
#include "workbench/selection_context.h"
#include "workbench/workbench_host.h"

// Full session-window types: the bench lifecycle hooks (#813) call each
// window's dirty/in-flight accessors directly.
#include "classification/qgsclassificationmainwindow.h"
#include "georeferencer/qgsgeoreferencermainwindow.h"
#include "georeferencer/qgsgeoref_image_to_map_window.h"

#include <QAction>
#include <QDockWidget>
#include <QMenuBar>
#include <QMenu>
#include <QStackedWidget>

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
            [this] { openClassificationWindow(); }, m_workbenchHost );
        bench->setWindowGetter( [this]() -> QWidget * { return m_classifyWindow; } );
        bench->setDirtyFn( [this] { return m_classifyWindow && m_classifyWindow->isSessionDirty(); } );
        bench->setInFlightFn( [this] { return m_classifyWindow && m_classifyWindow->hasInFlightCompute(); } );
        bench->setCancelFn( [this] {
            if ( !m_classifyWindow || !m_classifyWindow->hasInFlightCompute() )
                return false;
            m_classifyWindow->cancelInFlightCompute();
            return true;
        } );
        bench->setCloseFn( [this] {
            if ( !m_classifyWindow )
                return true;
            m_classifyWindow->close(); // its closeEvent confirms unsaved state
            return !m_classifyWindow->isVisible();
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
        [this] { newLayout(); }, m_workbenchHost ) );

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
    m_inspectorHost->attachSelectionContext( m_selectionContext );
    m_inspectorDock->setWidget( m_inspectorHost );
    addDockWidget( Qt::RightDockWidgetArea, m_inspectorDock );
    m_inspectorDock->hide(); // available on demand — no more permanent chrome
}
