// main_window_docks.cpp — Dock widget setup
// Extracted from main_window.cpp for maintainability
#include "main_window.h"

#include "layer_manager.h"
#include "log_panel.h"
#include "shell/job_engine_qt_bridge.h"
#include "shell/ribbon_controller.h"
#include "shell/rs_job_panel.h"
#include "shell/task_panel_host.h"
#include "shell/workflow_session_controller.h"
#include "widgets/spectral_profile_widget.h"
#include "widgets/guided_workflow_widget.h"
#include "widgets/histogram_stretch_widget.h"

#include <QVBoxLayout>
#include <QMenu>
#include <QTextBrowser>
#include <QAction>
#include <QFileInfo>
#include <QStatusBar>
#include <QToolBar>

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
        if ( LayerManager::isLikelyRasterPath( fileName ) )
            m_layerManager->loadRasterLayer( fileName );
        else
        {
            const QString suffix = QFileInfo( fileName ).suffix().toLower();
            if ( suffix == QLatin1String( "shp" ) || suffix == QLatin1String( "gpkg" )
                 || suffix == QLatin1String( "geojson" ) || suffix == QLatin1String( "kml" )
                 || suffix == QLatin1String( "gml" ) )
                m_layerManager->loadVectorLayer( fileName );
            else
                statusBar()->showMessage( tr( "Unsupported file type: %1" ).arg( suffix.isEmpty() ? fileName : suffix ), 3000 );
        }
    });

    // Tabify the left dock widgets
    tabifyDockWidget(m_layersDock, m_browserDock);
    m_layersDock->raise();

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
    m_histogramStretchDock->setWidget( m_histogramStretch );
    addDockWidget( Qt::RightDockWidgetArea, m_histogramStretchDock );
    tabifyDockWidget( m_spectralDock, m_histogramStretchDock );
    m_histogramStretchDock->hide();

    // Log Panel (Bottom, tabified)
    m_logDock = new LogPanel(this);
    m_logDock->setObjectName("logDock");
    addDockWidget(Qt::BottomDockWidgetArea, m_logDock);

    // Unified JobEngine panel (Bottom, tabified with Log)
    m_jobPanel = new RsJobPanel( this );
    addDockWidget( Qt::BottomDockWidgetArea, m_jobPanel );
    tabifyDockWidget( m_logDock, m_jobPanel );
    m_jobPanel->raise();
    JobEngineQtBridge::instance(); // ensure listener is installed

    // Guided Workflow Panel (Right, tabified with processing)
    auto *workflowWidget = new GuidedWorkflowWidget(this);
    m_workflowDock = new QgsDockWidget(this);
    m_workflowDock->setObjectName("workflowDock");
    m_workflowDock->setWindowTitle(tr("Guided Workflows"));
    m_workflowDock->setWidget(workflowWidget);
    addDockWidget(Qt::RightDockWidgetArea, m_workflowDock);
    tabifyDockWidget(m_processingDock, m_workflowDock);

    // Window menu — add dock toggle actions
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

    // Compact product ribbon under the menu bar (top chrome), not over the canvas.
    m_ribbonController = new RibbonController( this, this );
    m_ribbonBar = m_ribbonController->createRibbonBar();
    connect( m_ribbonController, &RibbonController::openWorkflowTool,
             this, &QgisDesktopWindow::openWorkflowTool );

    auto *ribbonHost = new QToolBar( tr( "功能区" ), this );
    ribbonHost->setObjectName( QStringLiteral( "rsRibbonHost" ) );
    ribbonHost->setMovable( false );
    ribbonHost->setFloatable( false );
    ribbonHost->setAllowedAreas( Qt::TopToolBarArea );
    ribbonHost->setContextMenuPolicy( Qt::PreventContextMenu );
    ribbonHost->setIconSize( QSize( 16, 16 ) );
    ribbonHost->setToolButtonStyle( Qt::ToolButtonIconOnly );
    ribbonHost->setMinimumHeight( 80 );
    ribbonHost->setMaximumHeight( 84 );
    ribbonHost->addWidget( m_ribbonBar );

    // Insert as the first top toolbar so it sits directly under the menu bar.
    if ( QToolBar *mapBar = findChild<QToolBar *>( QStringLiteral( "mapToolsToolBar" ) ) )
        insertToolBar( mapBar, ribbonHost );
    else
        addToolBar( Qt::TopToolBarArea, ribbonHost );
}

void QgisDesktopWindow::applyProductShellLayout()
{
    // Product shell: Ribbon (top) + map tools + layers are primary.
    // Hide chrome that duplicates the same entry points (classic RS toolbar,
    // file toolbar, processing toolbox stack, guided-workflow dock).

    if ( QToolBar *tb = findChild<QToolBar *>( QStringLiteral( "rsToolBar" ) ) )
        tb->hide();
    if ( QToolBar *tb = findChild<QToolBar *>( QStringLiteral( "fileToolBar" ) ) )
        tb->hide();
    // Keep compact 导航与显示 toolbar (pan/zoom + contrast stretch shortcuts).
    if ( QToolBar *tb = findChild<QToolBar *>( QStringLiteral( "mapToolsToolBar" ) ) )
    {
        tb->show();
        // Sit directly under the ribbon host.
        if ( QToolBar *ribbonHost = findChild<QToolBar *>( QStringLiteral( "rsRibbonHost" ) ) )
            insertToolBarBreak( ribbonHost );
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

    // Task panel only when a tool is open — do not leave an empty right dock open.
    if ( m_taskPanelDock && !m_taskPanelDock->isVisible() )
        m_taskPanelDock->hide();

    if ( m_ribbonBar )
        m_ribbonBar->show();
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
