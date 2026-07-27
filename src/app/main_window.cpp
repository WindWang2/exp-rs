#include "main_window.h"
#include "dialogs/dialog_help_catalog.h"
#include "active_view_host.h"
#include "project_context.h"
#include "map_tools/map_tool_manager.h"
#include "app_paths.h"
#include "qgis_app_facade.h"

#ifdef SICNU_EMBED_PYTHON
#include "widgets/python_script_editor.h"
#endif

// Vector editing map tools
#include "qgsmaptooladdfeature.h"
#include "qgsmaptooladdpart.h"
#include "qgsmaptooladdring.h"
#include "qgsmaptoolmovefeature.h"
#include "qgsmaptoolrotatefeature.h"
#include "qgsmaptoolscalefeature.h"
#include "qgsmaptooloffsetcurve.h"
#include "qgsmaptoolreshape.h"
#include "qgsmaptoolsplitfeatures.h"
#include "qgsmaptoolsplitparts.h"
#include "qgsmaptoolsimplify.h"
#include "qgsmaptoolreverseline.h"
#include "qgsmaptoolfillring.h"
#include "qgsmaptooldeletepart.h"
#include "qgsmaptooldeletering.h"
#include "qgsmaptooltrimextendfeature.h"
#include "qgsmaptoolchamferfillet.h"
#include "qgsmaptoolfeaturearray.h"
#include "selecttools/qgsmaptoolselect.h"

// Vertex editing
#include "vertextool/qgsvertextool.h"

// Vector editing infrastructure
#include <qgsadvanceddigitizingdockwidget.h>
#include <qgsmessagebar.h>
#include <qgsundowidget.h>
#include "qgsclipboard.h"
#include "qgsfeatureaction.h"
#include "qgsguivectorlayertools.h"

#include <QAction>
#include <QCoreApplication>
#include <QDebug>
#include <QDockWidget>
#include <QMenuBar>
#include <QSettings>
#include <QSplitter>
#include <QStatusBar>
#include <QVBoxLayout>
#include <QWidget>

// QGIS C++ includes
#include <qgsapplication.h>
#include <qgis.h>
#include <qgsmapcanvas.h>
#include <qgsproject.h>
#include <qgsmaptool.h>
#include <qgsmaptoolpan.h>
#include <qgsmaptoolzoom.h>
#include <qgsmaptoolidentify.h>
#include <layertree/qgslayertreeview.h>
#include <qgsdockwidget.h>

// Plugin system
#include <core/plugin_manager.h>
#include <core/interfaces/sicnu_plugin_interface.h>

#ifdef SICNU_EMBED_PYTHON
#include "python/qgis_python.h"
#include "python/sicnu_python_console.h"
#include "python/sicnu_python_api.h"
#endif

QgisDesktopWindow::QgisDesktopWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle( tr( "SICNU GEO RS — 遥感分析平台" ) );
    setToolTip( SicnuDialogHelp::shortForTool( QStringLiteral( "main_window" ), windowTitle() ) );
    setWhatsThis( SicnuDialogHelp::htmlForTool( QStringLiteral( "main_window" ), windowTitle() ) );
    setStatusTip( toolTip() );
    resize(1600, 1000);

    qDebug() << "Setting up UI...";
    setupUi();
    qDebug() << "Setting up map canvas...";
    setupMapCanvas();

    qDebug() << "Setting up menu...";
    setupMenu(); // builds detached QMenuBar (action host only — not shown)
    qDebug() << "Setting up toolbars...";
    setupToolbars();
    qDebug() << "Setting up dock widgets...";
    setupDockWidgets();

    // One project-scoped Data/Display authority for the main QGIS view.
    const sicnu::display::DisplayViewSpec mainViewSpec{
        m_mapCanvas,
        QgsProject::instance()->layerTreeRoot(),
        QgsProject::instance()->layerStore()
    };
    auto context = sicnu::app::ProjectContext::create( mainViewSpec );
    if ( context )
        m_projectContext = context.take();
    else
        qCritical() << "Failed to create project data context";

    // Temporary compatibility facade over the project Data/Display seam.
    m_activeViewHost = std::make_unique<ActiveViewHost>(
        m_mapCanvas, m_layerTreeView, m_overviewCanvas,
        m_projectContext ? &m_projectContext->dataManager() : nullptr,
        m_projectContext ? &m_projectContext->displayManager() : nullptr,
        m_projectContext ? m_projectContext->mainViewId()
                         : sicnu::display::DisplayViewId{},
        this );
    // Data Manager panel needs ProjectContext; setupDockWidgets runs earlier.
    qDebug() << "Setting up Data Manager panel...";
    setupDataManagerPanel();
    qDebug() << "Setting up ribbon and task panel...";
    setupRibbonAndTaskPanel();
    qDebug() << "Setting up status bar...";
    setupStatusBar();
    qDebug() << "Setting up connections...";
    setupConnections();
    qDebug() << "Restoring panel state...";
    restorePanelState();
    // Must run AFTER restoreState so old QSettings do not resurrect duplicate chrome.
    applyProductShellLayout();

    // Apply saved light/dark theme (Canopy Lab QSS)
    {
        QSettings settings;
        applyUiTheme( settings.value( QStringLiteral( "preferences/theme" ),
                                      QStringLiteral( "light" ) ).toString() );
    }

    // Load plugins
    qDebug() << "Loading plugins...";
    m_pluginManager = std::make_unique<PluginManager>(m_mapCanvas, m_layerTreeView);
    m_pluginManager->loadPlugins(QCoreApplication::applicationDirPath() + "/../plugins");
    for (const QString &pluginName : m_pluginManager->loadedPlugins()) {
        SicnuPluginInterface *plugin = m_pluginManager->plugin(pluginName);
        if (!plugin) continue;

        // Add plugin widgets as dock widgets
        if (QWidget *widget = plugin->createWidget(this)) {
            auto *dock = new QgsDockWidget(plugin->name(), this);
            dock->setObjectName("plugin_" + plugin->name().toLower().replace(" ", "_"));
            dock->setWidget(widget);
            addDockWidget(Qt::RightDockWidgetArea, dock);
        }
        // Plugin menus go on the detached bar (never QMainWindow::menuBar()).
        for (QAction *action : plugin->menuActions()) {
            appMenuBar()->addAction(action);
        }
        // Add plugin toolbar actions
        for (QAction *action : plugin->toolbarActions()) {
            statusBar()->showMessage(tr("Plugin '%1' loaded").arg(plugin->name()), 3000);
        }
    }

    // Re-assert top chrome after any code path that might have touched menuBar().
    applyProductShellLayout();

#ifdef SICNU_EMBED_PYTHON
    // Initialize Python API with map canvas and active view host
    SicnuPythonApi::instance().initialize(m_mapCanvas);
    SicnuPythonApi::instance().setActiveViewHost(m_activeViewHost.get());
#endif

    // Restore theme preference
    QSettings settings;
    QString theme = settings.value("preferences/theme", "light").toString();
    if (theme == "dark") {
        applyDarkPalette();
        qDebug() << "Dark theme applied";
    }

    // Initialize CRS display from project
    updateCrsDisplay();

    qDebug() << "Window initialized";
}

QgisDesktopWindow::~QgisDesktopWindow()
{
    // Tear down child windows that rebind QgisApp / own canvases first.
    // Use QWidget* so we don't need full type definitions here.
    auto disposeChildWindow = []( QWidget *w ) {
        if (!w)
            return;
        w->hide();
        w->setParent(nullptr);
        w->deleteLater();
    };
    disposeChildWindow(static_cast<QWidget *>(static_cast<void *>(m_classifyWindow)));
    m_classifyWindow = nullptr;
    disposeChildWindow(m_obiaWindow);
    m_obiaWindow = nullptr;
    disposeChildWindow(static_cast<QWidget *>(static_cast<void *>(m_georefI2I)));
    m_georefI2I = nullptr;
    disposeChildWindow(static_cast<QWidget *>(static_cast<void *>(m_georefI2M)));
    m_georefI2M = nullptr;

    // Stop map jobs and release the active map tool before unique_ptr members
    // and QObject children (canvas) are destroyed — prevents double-delete of
    // QgsMapTool objects parented to the canvas (exit SIGSEGV).
    if (m_mapCanvas) {
        m_mapCanvas->stopRendering();
        if (QgsMapTool *tool = m_mapCanvas->mapTool())
            m_mapCanvas->unsetMapTool(tool);
        m_mapCanvas->setLayers({});
    }

    // Detach the layer tree view from its model before m_activeViewHost (the
    // model's owner) is reset below. The view outlives the manager — it is a
    // child widget destroyed later by ~QObject — and would otherwise query a
    // dead model from its close/hide events (exit SIGSEGV).
    if ( m_layerTreeView )
        m_layerTreeView->QTreeView::setModel( nullptr );

    // Destroy tool owners while the canvas QObject still exists so tools can
    // safely reparent/unset. unique_ptr destruction order is reverse of
    // declaration; force explicit reset here for clarity.
    m_toolManager.reset();
    m_activeViewHost.reset();
    m_projectContext.reset();
    m_pluginManager.reset();
}

void QgisDesktopWindow::setupUi()
{
    QWidget *centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);

    QVBoxLayout *centralLayout = new QVBoxLayout(centralWidget);
    centralLayout->setContentsMargins(0, 0, 0, 0);
    centralLayout->setSpacing(0);

    // Horizontal split: main canvas | optional secondary Display View (Wave D).
    m_mapSplitter = new QSplitter( Qt::Horizontal, centralWidget );
    m_mapSplitter->setObjectName( QStringLiteral( "rsMapViewSplitter" ) );
    m_mapSplitter->setChildrenCollapsible( false );

    m_mapCanvasContainer = new QWidget( m_mapSplitter );
    m_mapCanvasContainer->setObjectName( QStringLiteral( "rsMainMapView" ) );
    m_mapCanvasContainer->setMinimumSize( 400, 300 );
    m_mapSplitter->addWidget( m_mapCanvasContainer );

    centralLayout->addWidget( m_mapSplitter );
}

void QgisDesktopWindow::setupMapCanvas()
{
    m_mapCanvas = new QgsMapCanvas(m_mapCanvasContainer);

    QVBoxLayout *layout = new QVBoxLayout(m_mapCanvasContainer);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_mapCanvas);

    // Performance Optimization (matching QGIS defaults)
    m_mapCanvas->setParallelRenderingEnabled(true);
    m_mapCanvas->setMapUpdateInterval(250);
    m_mapCanvas->setPreviewJobsEnabled(true);

    // Visual Settings (matching QGIS defaults)
    m_mapCanvas->setCanvasColor(QColor("#e9ecf0"));
    m_mapCanvas->enableAntiAliasing(true);
    m_mapCanvas->setSelectionColor(QColor(255, 255, 0, 100));

    // Vector editing infrastructure
    m_cadDock = new QgsAdvancedDigitizingDockWidget(m_mapCanvas, this);
    addDockWidget(Qt::LeftDockWidgetArea, m_cadDock);
    m_cadDock->hide();
    m_messageBar = new QgsMessageBar(this);
    m_clipboard = new QgsClipboard();
    m_clipboard->setParent( this );

    // Place message bar above the map canvas
    QVBoxLayout *canvasLayout = qobject_cast<QVBoxLayout*>(m_mapCanvasContainer->parentWidget() ? m_mapCanvasContainer->parentWidget()->layout() : nullptr);
    if (!canvasLayout)
    {
        // The central widget layout — insert message bar before canvas container
        if (QWidget *central = centralWidget())
        {
            if (QVBoxLayout *lay = qobject_cast<QVBoxLayout*>(central->layout()))
            {
                lay->insertWidget(0, m_messageBar);
            }
        }
    }

    // Initialize QgisApp facade for ported tools (wire clipboard so cut/copy/paste work)
    auto *vectorLayerTools = new QgsGuiVectorLayerTools();
    QgisApp::initialize( m_mapCanvas, m_cadDock, vectorLayerTools, m_messageBar, this, m_clipboard );

    // Map Tools Setup (Delegated to MapToolManager)
    m_toolManager = std::make_unique<MapToolManager>(this, m_mapCanvas, m_cadDock);
    m_toolManager->setupTools();

    // Assign tool pointers for backward compatibility and clean usage
    m_panTool = m_toolManager->panTool();
    m_zoomInTool = m_toolManager->zoomInTool();
    m_zoomOutTool = m_toolManager->zoomOutTool();
    m_identifyTool = m_toolManager->identifyTool();
    m_measureDistanceTool = m_toolManager->measureDistanceTool();
    m_measureAreaTool = m_toolManager->measureAreaTool();

    m_selectTool = m_toolManager->selectTool();
    m_addFeatureTool = m_toolManager->addFeatureTool();
    m_moveFeatureTool = m_toolManager->moveFeatureTool();
    m_rotateFeatureTool = m_toolManager->rotateFeatureTool();
    m_scaleFeatureTool = m_toolManager->scaleFeatureTool();
    m_offsetCurveTool = m_toolManager->offsetCurveTool();
    m_reshapeTool = m_toolManager->reshapeTool();
    m_splitFeaturesTool = m_toolManager->splitFeaturesTool();
    m_splitPartsTool = m_toolManager->splitPartsTool();
    m_simplifyTool = m_toolManager->simplifyTool();
    m_reverseLineTool = m_toolManager->reverseLineTool();
    m_addRingTool = m_toolManager->addRingTool();
    m_addPartTool = m_toolManager->addPartTool();
    m_fillRingTool = m_toolManager->fillRingTool();
    m_deletePartTool = m_toolManager->deletePartTool();
    m_deleteRingTool = m_toolManager->deleteRingTool();
    m_trimExtendTool = m_toolManager->trimExtendTool();
    m_chamferFilletTool = m_toolManager->chamferFilletTool();
    m_featureArrayTool = m_toolManager->featureArrayTool();
    m_vertexTool = m_toolManager->vertexTool();

    // Set default tool (QGIS default: pan)
    m_mapCanvas->setMapTool(m_panTool);
}
