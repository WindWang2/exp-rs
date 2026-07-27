// main_window_docks.cpp — Dock widget setup
// Extracted from main_window.cpp for maintainability
#include "main_window.h"

#include "layer_manager.h"
#include "log_panel.h"
#include "panels/data_manager_panel.h"
#include "panels/task_center_dock.h"
#include "project_context.h"
#include "data/data_manager.h"
#include "shell/job_engine_qt_bridge.h"
#include "shell/processing_job_adapter.h"
#include "shell/ribbon_controller.h"
#include "shell/rs_job_panel.h"
#include "shell/task_panel_host.h"
#include "shell/workflow_session_controller.h"
#include "widgets/spectral_profile_widget.h"
#include "widgets/guided_workflow_widget.h"
#include "widgets/histogram_stretch_widget.h"
#include "widgets/band_composition_rail.h"

#include <QVBoxLayout>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QTextBrowser>
#include <QAction>
#include <QEvent>
#include <QStatusBar>
#include <QToolBar>
#include <QSizePolicy>

#include <qgsapplication.h>
#include <processing/qgsprocessingalgorithm.h>
#include <processing/qgsprocessingfavoritealgorithmmanager.h>
#include <qgsbrowserdockwidget.h>
#include <qgsbrowserguimodel.h>
#include <qgsdockwidget.h>
#include <qgsfilterlineedit.h>
#include <qgsmapoverviewcanvas.h>
#include <qgsmaplayer.h>
#include <qgsproject.h>
#include <qgsprocessingtoolboxtreeview.h>
#include <qgsrasterlayer.h>
#include <qgsgui.h>

#ifdef SICNU_EMBED_PYTHON
#include "python/sicnu_python_console.h"
#include "widgets/python_script_editor.h"
#endif

void QgisDesktopWindow::setupDockWidgets()
{
    // Layers Panel (Left)
    m_layersDock = new QgsDockWidget("Layers", this);
    m_layersDock->setObjectName("layersDock");
    m_layersDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    QWidget *layersContainer = new QWidget(m_layersDock);
    QVBoxLayout *layersLayout = new QVBoxLayout(layersContainer);
    layersLayout->setContentsMargins(0, 0, 0, 0);

    // Create QGIS C++ layer tree view
    m_layerTreeView = new QgsLayerTreeView(layersContainer);
    m_layerTreeView->setHeaderHidden(false);

    layersLayout->addWidget(m_layerTreeView);

    m_layersDock->setWidget(layersContainer);
    addDockWidget(Qt::LeftDockWidgetArea, m_layersDock);

    // Browser Panel (Left, below layers)
    m_browserModel = new QgsBrowserGuiModel( this );
    m_browserDock = new QgsBrowserDockWidget( "Browser", m_browserModel, this );
    m_browserDock->setObjectName( "browserDock" );
    m_browserDock->setAllowedAreas( Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea );
    addDockWidget( Qt::LeftDockWidgetArea, m_browserDock );

    // Browser double-click / drag → add layer to project
    connect(m_browserDock, &QgsBrowserDockWidget::openFile, this, [this](const QString &fileName, const QString &fileTypeHint) {
        Q_UNUSED(fileTypeHint);
        if (fileName.isEmpty()) return;
        ( void ) m_layerManager->loadLayer( fileName );
    });

    // Tabify the left dock widgets
    tabifyDockWidget(m_layersDock, m_browserDock);
    m_layersDock->raise();
    // Data Manager panel is created later in setupDataManagerPanel() once
    // ProjectContext exists (setupDockWidgets runs before context creation).

    // Processing Toolbox Panel (Right, with Overview)
    m_processingDock = new QgsDockWidget("Processing Toolbox", this);
    m_processingDock->setObjectName("processingDock");
    m_processingDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    // Container with search box + tree view
    auto *toolboxContainer = new QWidget(m_processingDock);
    auto *toolboxLayout = new QVBoxLayout(toolboxContainer);
    toolboxLayout->setContentsMargins(0, 0, 0, 0);
    toolboxLayout->setSpacing(2);

    auto *searchEdit = new QgsFilterLineEdit(toolboxContainer);
    searchEdit->setShowSearchIcon(true);
    searchEdit->setPlaceholderText(tr("Search algorithms..."));
    toolboxLayout->addWidget(searchEdit);

    m_toolboxView = new QgsProcessingToolboxTreeView( toolboxContainer,
        QgsApplication::processingRegistry(),
        QgsGui::processingRecentAlgorithmLog(),
        QgsGui::processingFavoriteAlgorithmManager() );
    m_toolboxView->setRegistry( QgsApplication::processingRegistry(),
        QgsGui::processingRecentAlgorithmLog(),
        QgsGui::processingFavoriteAlgorithmManager() );
    toolboxLayout->addWidget(m_toolboxView);

    m_processingDock->setWidget(toolboxContainer);
    addDockWidget(Qt::RightDockWidgetArea, m_processingDock);

    // Connect search filter
    connect(searchEdit, &QgsFilterLineEdit::textChanged,
            m_toolboxView, &QgsProcessingToolboxTreeView::setFilterString);

#ifdef SICNU_EMBED_PYTHON
    // Python Console (lazy-loaded on first use)
    m_pythonDock = new QgsDockWidget(tr("Python Console"), this);
    m_pythonDock->setObjectName("pythonDock");
    m_pythonDock->setWidget(new QWidget(m_pythonDock)); // Placeholder
    addDockWidget(Qt::BottomDockWidgetArea, m_pythonDock);
    m_pythonDock->hide(); // Hidden until first use

    // Python Script Editor Dock (lazy-loaded on first use)
    m_pythonScriptEditorDock = new QgsDockWidget(tr("Python Script Editor"), this);
    m_pythonScriptEditorDock->setObjectName("pythonScriptEditorDock");
    m_pythonScriptEditorDock->setWidget(new QWidget(m_pythonScriptEditorDock)); // Placeholder
    addDockWidget(Qt::BottomDockWidgetArea, m_pythonScriptEditorDock);
    tabifyDockWidget(m_pythonDock, m_pythonScriptEditorDock);
    m_pythonScriptEditorDock->hide(); // Hidden until first use
#endif

    // Double-click on algorithm in toolbox opens execution dialog
    connect(m_toolboxView, &QgsProcessingToolboxTreeView::doubleClicked, this, [this](const QModelIndex &index) {
        const QgsProcessingAlgorithm *alg = m_toolboxView->algorithmForIndex(index);
        if (!alg)
            return;

        openProcessingAlgorithm(alg->id());
    });

    // Right-click context menu for favorites
    m_toolboxView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_toolboxView, &QgsProcessingToolboxTreeView::customContextMenuRequested, this,
        [this](const QPoint &pos) {
            QModelIndex index = m_toolboxView->indexAt(pos);
            const QgsProcessingAlgorithm *alg = m_toolboxView->algorithmForIndex(index);
            if (!alg)
                return;

            QMenu menu(this);
            QString algId = alg->id();

            // Check if already in favorites
            bool isFav = QgsGui::processingFavoriteAlgorithmManager()->isFavorite(algId);
            if (isFav) {
                QAction *removeFav = menu.addAction(tr("Remove from Favorites"));
                connect(removeFav, &QAction::triggered, this, [this, algId]() {
                    QgsGui::processingFavoriteAlgorithmManager()->remove(algId);
                    m_toolboxView->setRegistry(QgsApplication::processingRegistry(),
                        QgsGui::processingRecentAlgorithmLog(),
                        QgsGui::processingFavoriteAlgorithmManager());
                });
            } else {
                QAction *addFav = menu.addAction(tr("Add to Favorites"));
                connect(addFav, &QAction::triggered, this, [this, algId]() {
                    QgsGui::processingFavoriteAlgorithmManager()->add(algId);
                    m_toolboxView->setRegistry(QgsApplication::processingRegistry(),
                        QgsGui::processingRecentAlgorithmLog(),
                        QgsGui::processingFavoriteAlgorithmManager());
                });
            }

            // Open algorithm action
            QAction *openAlg = menu.addAction(tr("Open Algorithm"));
            connect(openAlg, &QAction::triggered, this, [this, algId]() {
                openProcessingAlgorithm(algId);
            });

            menu.exec(m_toolboxView->viewport()->mapToGlobal(pos));
        });


    // Overview Panel (Right, tabified with Processing Toolbox)
    m_overviewDock = new QgsDockWidget("Overview", this);
    m_overviewDock->setObjectName("overviewDock");
    m_overviewDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    m_overviewCanvas = new QgsMapOverviewCanvas(m_overviewDock, m_mapCanvas);
    m_overviewCanvas->enableAntiAliasing(true);
    m_overviewDock->setWidget(m_overviewCanvas);
    addDockWidget(Qt::RightDockWidgetArea, m_overviewDock);
    tabifyDockWidget(m_processingDock, m_overviewDock);
    m_processingDock->raise();

    // Identify Results Panel (Right, tabified with Processing/Overview)
    m_identifyDock = new QgsDockWidget(tr("Identify Results"), this);
    m_identifyDock->setObjectName("identifyDock");
    m_identifyDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    m_identifyResults = new QTextBrowser(m_identifyDock);
    m_identifyResults->setOpenExternalLinks(false);
    m_identifyResults->setPlaceholderText(tr("Click on the map with the Identify tool to see feature details here."));
    m_identifyDock->setWidget(m_identifyResults);
    addDockWidget(Qt::RightDockWidgetArea, m_identifyDock);
    tabifyDockWidget(m_overviewDock, m_identifyDock);

    // Spectral Profile Panel (Right, tabified with Identify Results)
    m_spectralDock = new QgsDockWidget(tr("Spectral Profile"), this);
    m_spectralDock->setObjectName("spectralDock");
    m_spectralDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    m_spectralProfile = new SpectralProfileWidget(m_spectralDock);
    m_spectralDock->setWidget(m_spectralProfile);
    addDockWidget(Qt::RightDockWidgetArea, m_spectralDock);
    tabifyDockWidget(m_identifyDock, m_spectralDock);

    // Display stretch panel (renderer only — no export; like layer symbology stretch)
    m_histogramStretchDock = new QgsDockWidget( tr( "显示拉伸" ), this );
    m_histogramStretchDock->setObjectName( "histogramStretchDock" );
    m_histogramStretchDock->setAllowedAreas( Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea );

    m_histogramStretch = new HistogramStretchWidget( m_histogramStretchDock );
    connect( m_histogramStretch, &HistogramStretchWidget::stretchApplied,
             this, [this]
    {
        // The renderer is replaced by the stretch widget. Force the canvas to
        // schedule a fresh map-render job after that replacement instead of
        // relying solely on the layer repaint notification.
        if ( m_mapCanvas )
            m_mapCanvas->refresh();
    } );
    m_histogramStretchDock->setWidget( m_histogramStretch );
    addDockWidget( Qt::RightDockWidgetArea, m_histogramStretchDock );
    tabifyDockWidget( m_spectralDock, m_histogramStretchDock );
    m_histogramStretchDock->hide();

    // Log Panel (Bottom, tabified)
    m_logDock = new LogPanel(this);
    m_logDock->setObjectName("logDock");
    addDockWidget(Qt::BottomDockWidgetArea, m_logDock);

    // Unified Task Center projection panel (Bottom, tabified with Log)
    m_jobPanel = new RsJobPanel( this );
    addDockWidget( Qt::BottomDockWidgetArea, m_jobPanel );
    tabifyDockWidget( m_logDock, m_jobPanel );
    m_jobPanel->raise();
    JobEngineQtBridge::instance(); // keep Qt bridge for internal JobEngine events
    ProcessingJobAdapter::registerProcessingJobExecutor();

    // Guided Workflow Panel (Right, tabified with processing)
    auto *workflowWidget = new GuidedWorkflowWidget(this);
    m_workflowDock = new QgsDockWidget(this);
    m_workflowDock->setObjectName("workflowDock");
    m_workflowDock->setWindowTitle(tr("Guided Workflows"));
    m_workflowDock->setWidget(workflowWidget);
    addDockWidget(Qt::RightDockWidgetArea, m_workflowDock);
    tabifyDockWidget(m_processingDock, m_workflowDock);

    // Window menu — add dock toggle actions
    // Data Manager toggle is added in setupDataManagerPanel() (created later).
    if (m_windowMenu) {
        m_windowMenu->addSeparator();
        m_windowMenu->addAction(m_layersDock->toggleViewAction());
        m_windowMenu->addAction(m_browserDock->toggleViewAction());
        m_windowMenu->addAction(m_processingDock->toggleViewAction());
        m_windowMenu->addAction(m_overviewDock->toggleViewAction());
        m_windowMenu->addAction(m_identifyDock->toggleViewAction());
        m_windowMenu->addAction(m_spectralDock->toggleViewAction());
        m_windowMenu->addAction(m_histogramStretchDock->toggleViewAction());
        m_windowMenu->addAction(m_logDock->toggleViewAction());
        if ( m_jobPanel )
          m_windowMenu->addAction( m_jobPanel->toggleViewAction() );
        m_windowMenu->addAction(m_workflowDock->toggleViewAction());
        // Task panel dock is created after setupDockWidgets (setupRibbonAndTaskPanel);
        // its toggle action is added there once the dock exists.
        m_windowMenu->addSeparator();

#ifdef SICNU_EMBED_PYTHON
        // Python Console (lazy-loaded)
        QAction *pythonAction = m_windowMenu->addAction(tr("Python Console"));
        pythonAction->setCheckable(true);
        connect(pythonAction, &QAction::triggered, this, [this]() {
            if (!m_pythonConsole) {
                statusBar()->showMessage(tr("Initializing Python..."));
                m_pythonConsole = std::make_unique<SicnuPythonConsole>(m_pythonDock);
                m_pythonDock->setWidget(m_pythonConsole.get());
                statusBar()->showMessage(tr("Python ready"), 3000);
            }
            m_pythonDock->show();
            m_pythonDock->raise();
        });

        // Python Script Editor (lazy-loaded)
        QAction *scriptEditorAction = m_windowMenu->addAction(tr("Python Script Editor"));
        scriptEditorAction->setCheckable(true);
        connect(scriptEditorAction, &QAction::triggered, this, [this]() {
            if (!m_pythonScriptEditor) {
                statusBar()->showMessage(tr("Initializing Python script editor..."));
                m_pythonScriptEditor = std::make_unique<Sicnu::PythonScriptEditor>(m_pythonScriptEditorDock);
                connect(m_pythonScriptEditor.get(), &Sicnu::PythonScriptEditor::statusMessage,
                        this, [this](const QString &message) {
                            statusBar()->showMessage(message, 3000);
                        });
                m_pythonScriptEditorDock->setWidget(m_pythonScriptEditor.get());
                statusBar()->showMessage(tr("Python script editor ready"), 3000);
            }
            m_pythonScriptEditorDock->show();
            m_pythonScriptEditorDock->raise();
        });

        m_windowMenu->addSeparator();
#endif
        QAction *resetLayoutAction = m_windowMenu->addAction(tr("Reset Layout"));
        connect(resetLayoutAction, &QAction::triggered, this, &QgisDesktopWindow::resetPanelLayout);
    }
}

void QgisDesktopWindow::setupDataManagerPanel()
{
    // Must run after ProjectContext is created. Catalog is separate from the layer tree.
    if ( !m_projectContext || m_dataManagerPanel )
        return;

    m_dataManagerPanel =
        new sicnu::DataManagerPanel( &m_projectContext->dataManager(), this );
    addDockWidget( Qt::LeftDockWidgetArea, m_dataManagerPanel );
    if ( m_layersDock )
        tabifyDockWidget( m_layersDock, m_dataManagerPanel );

    connect( m_dataManagerPanel, &sicnu::DataManagerPanel::displayRequested,
             this, [this]( sicnu::data::AssetId assetId ) {
        if ( !m_projectContext )
            return;
        const auto added = m_projectContext->displayManager().addLayer(
            m_projectContext->mainViewId(), assetId );
        if ( !added )
        {
            QMessageBox::warning(
                this, tr( "添加到显示" ),
                tr( "无法将数据资产添加到地图显示。" ) );
        }
    } );

    auto unloadOne = [this]( sicnu::data::AssetId assetId, bool confirm ) -> bool {
        if ( !m_projectContext )
            return false;
        sicnu::data::DataManager &dataManager = m_projectContext->dataManager();
        const sicnu::data::UnloadPlan plan = dataManager.planUnload( assetId );
        if ( confirm )
        {
            QString detail = tr( "从工程卸载此数据资产？" );
            if ( !plan.activeLeases().isEmpty() )
            {
                detail = tr( "该资产正被 %1 个显示/处理租约引用。卸载将移除对应呈现。\n\n"
                             "继续级联卸载？" )
                             .arg( plan.activeLeases().size() );
            }
            const auto choice = QMessageBox::question(
                this, tr( "卸载数据资产" ), detail,
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No );
            if ( choice != QMessageBox::Yes )
                return false;
        }
        const sicnu::data::UnloadPlan confirmed =
            plan.activeLeases().isEmpty() ? plan : plan.confirmedCascade();
        return static_cast<bool>( dataManager.unload( confirmed ) );
    };

    connect( m_dataManagerPanel, &sicnu::DataManagerPanel::unloadRequested,
             this, [this, unloadOne]( sicnu::data::AssetId assetId ) {
        if ( !m_projectContext )
            return;
        // Confirm path: if the user accepts and unload fails, surface a warning.
        sicnu::data::DataManager &dataManager = m_projectContext->dataManager();
        const sicnu::data::UnloadPlan plan = dataManager.planUnload( assetId );
        QString detail = tr( "从工程卸载此数据资产？" );
        if ( !plan.activeLeases().isEmpty() )
        {
            detail = tr( "该资产正被 %1 个显示/处理租约引用。卸载将移除对应呈现。\n\n"
                         "继续级联卸载？" )
                         .arg( plan.activeLeases().size() );
        }
        const auto choice = QMessageBox::question(
            this, tr( "卸载数据资产" ), detail,
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No );
        if ( choice != QMessageBox::Yes )
            return;
        if ( !unloadOne( assetId, false ) )
        {
            QMessageBox::warning(
                this, tr( "卸载数据资产" ),
                tr( "无法卸载该数据资产。" ) );
        }
    } );

    connect( m_dataManagerPanel, &sicnu::DataManagerPanel::unloadRequestedMany,
             this, [this, unloadOne]( const QList<sicnu::data::AssetId> &ids ) {
        if ( ids.isEmpty() || !m_projectContext )
            return;
        const auto choice = QMessageBox::question(
            this, tr( "批量卸载" ),
            tr( "从工程卸载选中的 %1 个数据资产？\n"
                "若存在显示/处理引用，将级联移除对应呈现。" )
              .arg( ids.size() ),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No );
        if ( choice != QMessageBox::Yes )
            return;

        int ok = 0;
        int failed = 0;
        for ( const sicnu::data::AssetId &id : ids )
        {
            if ( unloadOne( id, false ) )
                ++ok;
            else
                ++failed;
        }
        if ( failed > 0 )
        {
            QMessageBox::warning(
                this, tr( "批量卸载" ),
                tr( "完成：成功 %1，失败 %2。" ).arg( ok ).arg( failed ) );
        }
        else if ( statusBar() )
        {
            statusBar()->showMessage( tr( "已卸载 %1 个数据资产" ).arg( ok ), 4000 );
        }
    } );

    connect( m_dataManagerPanel, &sicnu::DataManagerPanel::promoteRequested,
             this, [this]( sicnu::data::AssetId assetId ) {
        if ( !m_projectContext )
            return;
        const auto promoted = m_projectContext->dataManager().promote( assetId );
        if ( !promoted )
        {
            QMessageBox::warning(
                this, tr( "提升为工程持久" ),
                tr( "无法提升该临时数据资产。" ) );
        }
    } );

    if ( m_windowMenu )
        m_windowMenu->addAction( m_dataManagerPanel->toggleViewAction() );
}

void QgisDesktopWindow::showDataManagerPanel()
{
    if ( !m_dataManagerPanel )
        setupDataManagerPanel();
    if ( !m_dataManagerPanel )
        return;
    m_dataManagerPanel->show();
    m_dataManagerPanel->raise();
}

void QgisDesktopWindow::setupRibbonAndTaskPanel()
{
    // Right-side task panel for atomic workflow tools (primary RS tool surface).
    // Do not tabify with Processing Toolbox — that stack made two UIs fight for the
    // same dock area. Processing stays available from 窗口 menu for experts.
    m_taskPanel = new TaskPanelHost( this );
    m_taskPanelDock = new QgsDockWidget( tr( "任务" ), this );
    m_taskPanelDock->setObjectName( QStringLiteral( "rsTaskPanelDock" ) );
    m_taskPanelDock->setAllowedAreas( Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea );
    m_taskPanelDock->setWidget( m_taskPanel );
    addDockWidget( Qt::RightDockWidgetArea, m_taskPanelDock );
    // Start hidden; openWorkflowTool() shows it when a tool is chosen.
    m_taskPanelDock->hide();

    if ( m_windowMenu )
        m_windowMenu->addAction( m_taskPanelDock->toggleViewAction() );

    m_taskCenterDock = new sicnu::TaskCenterDock( this );
    addDockWidget( Qt::RightDockWidgetArea, m_taskCenterDock );
    if ( m_windowMenu )
        m_windowMenu->addAction( m_taskCenterDock->toggleViewAction() );

    connect( m_taskCenterDock, &sicnu::TaskCenterDock::layerAutoLoadRequested,
             this, [this]( const QString &path ) {
                 loadRasterLayer( path );
             } );

    // Session controller bridges TaskPanelHost ↔ WorkflowRuntime
    m_sessionController = new WorkflowSessionController( this );
    m_sessionController->registerBuiltins();
    m_sessionController->bindPanel( m_taskPanel );

    connect( m_sessionController, &WorkflowSessionController::requestLoadRaster,
             this, [this]( const QString &path ) {
                 loadRasterLayer( path );
             } );
    connect( m_sessionController, &WorkflowSessionController::statusMessage,
             this, [this]( const QString &msg ) {
                 statusBar()->showMessage( msg, 5000 );
             } );
    connect( m_taskPanel, &TaskPanelHost::closeClicked, this, [this]() {
        if ( m_taskPanelDock )
            m_taskPanelDock->hide();
    } );

    // ArcGIS Pro style: full-width top chrome ABOVE left/right docks.
    // setMenuWidget() only gets a menubar-height slot in practice and compresses
    // the ribbon; top-dock + setCorner(Top*Corner, TopDock) is the reliable
    // way to 置顶拉通 across the whole window.
    m_ribbonController = new RibbonController( this, this );
    m_ribbonBar = m_ribbonController->createRibbonBar();
    connect( m_ribbonController, &RibbonController::openWorkflowTool,
             this, &QgisDesktopWindow::openWorkflowTool );

    auto *chrome = new QWidget;
    chrome->setObjectName( QStringLiteral( "rsTopChrome" ) );
    chrome->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Fixed );
    // QAT(28) + tabs(30) + content(96) + band rail(30) = 184
    chrome->setFixedHeight( 184 );

    auto *chromeLay = new QVBoxLayout( chrome );
    chromeLay->setContentsMargins( 0, 0, 0, 0 );
    chromeLay->setSpacing( 0 );
    chromeLay->addWidget( m_ribbonBar );

    m_bandRail = new BandCompositionRail( chrome );
    m_bandRail->setFixedHeight( 30 );
    chromeLay->addWidget( m_bandRail );

    m_topChrome = chrome;

    // Corners claim the top strip so the dock spans over left/right docks.
    setCorner( Qt::TopLeftCorner, Qt::TopDockWidgetArea );
    setCorner( Qt::TopRightCorner, Qt::TopDockWidgetArea );

    auto *ribbonDock = new QDockWidget( this );
    ribbonDock->setObjectName( QStringLiteral( "rsRibbonDock" ) );
    ribbonDock->setWindowTitle( QString() );
    ribbonDock->setFeatures( QDockWidget::NoDockWidgetFeatures );
    ribbonDock->setAllowedAreas( Qt::TopDockWidgetArea );
    ribbonDock->setTitleBarWidget( new QWidget( ribbonDock ) ); // no title chrome
    ribbonDock->setWidget( chrome );
    ribbonDock->setFixedHeight( 184 );
    ribbonDock->setMinimumHeight( 184 );
    ribbonDock->setMaximumHeight( 184 );
    addDockWidget( Qt::TopDockWidgetArea, ribbonDock );
    // Ensure no menu-widget leftovers steal vertical space.
    setMenuWidget( nullptr );
}

void QgisDesktopWindow::applyProductShellLayout()
{
    // Product shell = ArcGIS Pro style: full-width Ribbon at top. No classic QToolBars.
    // All former toolbar actions live on Ribbon tabs (地图 / 编辑 / 数据 / …).

    // Full-width top strip over left/right docks.
    setCorner( Qt::TopLeftCorner, Qt::TopDockWidgetArea );
    setCorner( Qt::TopRightCorner, Qt::TopDockWidgetArea );
    setMenuWidget( nullptr );

    // Detach + hide every QToolBar (including restoreState leftovers).
    const QList<QToolBar *> bars = findChildren<QToolBar *>();
    for ( QToolBar *tb : bars )
    {
        if ( !tb )
            continue;
        tb->setVisible( false );
        tb->hide();
        removeToolBar( tb );
    }

    auto hideDock = []( QDockWidget *dock ) {
        if ( dock )
            dock->hide();
    };
    hideDock( m_processingDock );
    hideDock( m_workflowDock );
    hideDock( m_overviewDock );
    hideDock( m_identifyDock );
    hideDock( m_spectralDock );
    hideDock( m_histogramStretchDock );
    // Log stays available but collapsed by default to reduce vertical noise.
    hideDock( m_logDock );
    // Job panel is primary observability surface — show by default.
    if ( m_jobPanel )
    {
      m_jobPanel->show();
      m_jobPanel->raise();
    }

    // Layers stay on the left; browser tabified under it can stay hidden until needed.
    if ( m_browserDock )
        m_browserDock->hide();
    if ( m_layersDock )
    {
        m_layersDock->show();
        m_layersDock->raise();
    }
    // Data Manager stays available as a left tab (catalog ≠ layer tree).
    if ( m_dataManagerPanel )
        m_dataManagerPanel->show();

    // Task panel only when a tool is open — do not leave an empty right dock open.
    if ( m_taskPanelDock && !m_taskPanelDock->isVisible() )
        m_taskPanelDock->hide();

    // Pin ribbon dock to top with fixed product height.
    if ( QDockWidget *ribbonDock = findChild<QDockWidget *>( QStringLiteral( "rsRibbonDock" ) ) )
    {
        ribbonDock->setFeatures( QDockWidget::NoDockWidgetFeatures );
        ribbonDock->setAllowedAreas( Qt::TopDockWidgetArea );
        if ( !ribbonDock->titleBarWidget() )
            ribbonDock->setTitleBarWidget( new QWidget( ribbonDock ) );
        ribbonDock->setFixedHeight( 184 );
        ribbonDock->show();
        addDockWidget( Qt::TopDockWidgetArea, ribbonDock );
        ribbonDock->raise();
    }
    if ( m_topChrome )
    {
        m_topChrome->setFixedHeight( 184 );
        m_topChrome->show();
    }
    if ( m_ribbonBar )
        m_ribbonBar->show();
    if ( m_bandRail )
        m_bandRail->show();

    // Keep the detached action-host menubar hidden (never install it).
    if ( m_hiddenMenuBar )
    {
        m_hiddenMenuBar->setNativeMenuBar( false );
        m_hiddenMenuBar->setMaximumHeight( 0 );
        m_hiddenMenuBar->hide();
    }
}

void QgisDesktopWindow::refreshWorkflowLayerChoices()
{
    if ( !m_sessionController )
        return;

    QStringList ids;
    QStringList names;
    const QMap<QString, QgsMapLayer *> layers = QgsProject::instance()->mapLayers();
    for ( auto it = layers.constBegin(); it != layers.constEnd(); ++it )
    {
        QgsMapLayer *layer = it.value();
        if ( !layer || !qobject_cast<QgsRasterLayer *>( layer ) )
            continue;
        ids.append( it.key() );
        names.append( layer->name() );
    }
    m_sessionController->setLayerChoices( ids, names );
}

void QgisDesktopWindow::openWorkflowTool( const QString &definitionId )
{
    if ( !m_sessionController || !m_taskPanelDock )
        return;

    refreshWorkflowLayerChoices();
    m_sessionController->openTool( definitionId );
    m_taskPanelDock->show();
    m_taskPanelDock->raise();
}
