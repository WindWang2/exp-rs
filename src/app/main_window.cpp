#include "main_window.h"
#include "layer_tree_menu.h"
#include "app_paths.h"
#include "dialogs/band_math_dialog.h"
#include "dialogs/spectral_index_dialog.h"
#include "dialogs/atmospheric_dialog.h"
#include "dialogs/crs_preset_dialog.h"
#include "dialogs/preferences_dialog.h"
#include "dialogs/contrast_stretch_dialog.h"
#include "dialogs/spatial_filter_dialog.h"
#include "dialogs/pca_dialog.h"
#include "dialogs/band_ratio_dialog.h"
#include "widgets/spectral_profile_widget.h"
#include "log_panel.h"
#include "georeferencer/qgsgeoreferencermainwindow.h"
#ifdef SICNU_HAS_CLASSIFY
#include "classification/qgsclassificationmainwindow.h"
#endif

#include <processing/qgsprocessingparameters.h>

#include <QMenuBar>
#include <QToolBar>
#include <QStatusBar>
#include <QDockWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QAction>
#include <QLabel>
#include <QFileDialog>
#include <QMessageBox>
#include <QSplitter>
#include <QTimer>
#include <QFileInfo>
#include <QMenu>
#include <QHeaderView>
#include <QStyleFactory>
#include <QPalette>
#include <QInputDialog>
#include <QTabWidget>
#include <QFormLayout>
#include <QSettings>
#include <QPalette>
#include <QGroupBox>
#include <QSlider>
#include <QComboBox>
#include <QTextEdit>
#include <QTextBrowser>
#include <QListWidget>
#include <QDialogButtonBox>
#include <QCheckBox>
#include <QPushButton>
#include <QLineEdit>
#include <QSpinBox>
#include <QDoubleSpinBox>

// QGIS C++ includes
#include <qgsapplication.h>
#include <qgis.h>
#include <qgsmapcanvas.h>
#include <qgsmaptoolpan.h>
#include <qgsmaptoolzoom.h>
#include <qgsmaptoolidentify.h>
#include <layertree/qgslayertreeview.h>
#include <layertree/qgslayertreemodel.h>
#include <layertree/qgslayertreeviewdefaultactions.h>
#include <qgsdockwidget.h>
#include <qgsbrowserdockwidget.h>
#include <qgsbrowserguimodel.h>
#include <qgsproject.h>
#include <qgsmaplayer.h>
#include <qgsrasterlayer.h>
#include <qgsvectorlayer.h>
#include <qgslayertree.h>
#include <qgslayertreegroup.h>
#include <qgslayertreelayer.h>
#include <qgslayertreenode.h>

// Layer properties dialogs
#include <raster/qgsrasterlayerproperties.h>
#include <vector/qgsvectorlayerproperties.h>
#include <qgslayerpropertiesdialog.h>

// CRS/Projection selection
#include <proj/qgsprojectionselectionwidget.h>
#include <proj/qgsprojectionselectiondialog.h>

// QgsGui singleton
#include <qgsgui.h>

// Raster renderer and filters
#include <raster/qgsbrightnesscontrastfilter.h>
#include <raster/qgshuesaturationfilter.h>
#include <raster/qgssinglebandgrayrenderer.h>
#include <raster/qgssinglebandpseudocolorrenderer.h>
#include <raster/qgsrasterrenderer.h>
#include <raster/qgscontrastenhancement.h>

// Map renderer for performance
#include <qgsmaprenderersequentialjob.h>
#include <qgsmaprendererparalleljob.h>

// Processing framework
#include <processing/qgsprocessingregistry.h>
#include <processing/qgsprocessingtoolboxtreeview.h>
#include <processing/qgsprocessingalgorithmdialogbase.h>

// Python embedding (disabled — Python runtime removed)
// #include "python/qgis_python.h"
// #include "gui/python_console_widget.h"

QgisDesktopWindow::QgisDesktopWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle("RS Studio — Remote Sensing Analysis");
    resize(1600, 1000);

    qDebug() << "Setting up UI...";
    setupUi();
    qDebug() << "Setting up menu...";
    setupMenu();
    qDebug() << "Setting up toolbars...";
    setupToolbars();
    qDebug() << "Setting up dock widgets...";
    setupDockWidgets();
    qDebug() << "Setting up status bar...";
    setupStatusBar();
    qDebug() << "Setting up connections...";
    setupConnections();
    qDebug() << "Setting up map canvas...";
    setupMapCanvas();
    qDebug() << "Restoring panel state...";
    restorePanelState();

    // Restore theme preference
    QSettings settings;
    QString theme = settings.value("preferences/theme", "light").toString();
    if (theme == "dark") {
        // Apply dark palette
        QPalette darkPalette;
        darkPalette.setColor(QPalette::Window, QColor(53, 53, 53));
        darkPalette.setColor(QPalette::WindowText, Qt::white);
        darkPalette.setColor(QPalette::Base, QColor(25, 25, 25));
        darkPalette.setColor(QPalette::AlternateBase, QColor(53, 53, 53));
        darkPalette.setColor(QPalette::ToolTipBase, Qt::white);
        darkPalette.setColor(QPalette::ToolTipText, Qt::white);
        darkPalette.setColor(QPalette::Text, Qt::white);
        darkPalette.setColor(QPalette::Button, QColor(53, 53, 53));
        darkPalette.setColor(QPalette::ButtonText, Qt::white);
        darkPalette.setColor(QPalette::BrightText, Qt::red);
        darkPalette.setColor(QPalette::Link, QColor(42, 130, 218));
        darkPalette.setColor(QPalette::Highlight, QColor(42, 130, 218));
        darkPalette.setColor(QPalette::HighlightedText, Qt::black);
        qApp->setPalette(darkPalette);
        qDebug() << "Dark theme applied";
    }

    qDebug() << "Window initialized";
}

void QgisDesktopWindow::setupUi()
{
    QWidget *centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);

    QVBoxLayout *centralLayout = new QVBoxLayout(centralWidget);
    centralLayout->setContentsMargins(0, 0, 0, 0);
    centralLayout->setSpacing(0);

    m_mapCanvasContainer = new QWidget(centralWidget);
    m_mapCanvasContainer->setMinimumSize(800, 600);
    centralLayout->addWidget(m_mapCanvasContainer);
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

    // Map Tools
    m_panTool = new QgsMapToolPan(m_mapCanvas);
    m_zoomInTool = new QgsMapToolZoom(m_mapCanvas, false);
    m_zoomOutTool = new QgsMapToolZoom(m_mapCanvas, true);
    m_identifyTool = new CustomIdentifyTool(m_mapCanvas);
    m_measureDistanceTool = new MeasureTool( m_mapCanvas, MeasureTool::Distance, this );
    m_measureAreaTool = new MeasureTool( m_mapCanvas, MeasureTool::Area, this );

    // Set default tool (QGIS default: pan)
    m_mapCanvas->setMapTool(m_panTool);

    // Overview canvas (created here because it needs m_mapCanvas)
    m_overviewCanvas = new QgsMapOverviewCanvas(m_overviewDock, m_mapCanvas);
    m_overviewCanvas->enableAntiAliasing(true);
    m_overviewDock->setWidget(m_overviewCanvas);
}

void QgisDesktopWindow::setupMenu()
{
    // Brand logo (left corner)
    QWidget *brandWidget = new QWidget(this);
    brandWidget->setObjectName("rsMenuBarBrand");
    QHBoxLayout *brandLayout = new QHBoxLayout(brandWidget);
    brandLayout->setContentsMargins(8, 0, 0, 0);
    brandLayout->setSpacing(4);
    QLabel *logo = new QLabel("RS");
    logo->setObjectName("rsBrandLogo");
    QLabel *name = new QLabel("RS Studio");
    name->setObjectName("rsBrandName");
    brandLayout->addWidget(logo);
    brandLayout->addWidget(name);
    menuBar()->setCornerWidget(brandWidget, Qt::TopLeftCorner);

    // Version label (right corner)
    QLabel *versionLabel = new QLabel("v0.9.2-dev");
    versionLabel->setObjectName("rsBrandVersion");
    menuBar()->setCornerWidget(versionLabel, Qt::TopRightCorner);

    // Project Menu
    QMenu *projectMenu = menuBar()->addMenu(tr("&Project"));
    projectMenu->addAction(QIcon(":/icons/new_project"), tr("New Project"), this, &QgisDesktopWindow::newProject, QKeySequence::New);
    projectMenu->addAction(QIcon(":/icons/o_en"), tr("Open Project..."), this, &QgisDesktopWindow::openProject, QKeySequence::Open);
    projectMenu->addAction(QIcon(":/icons/s_ve"), tr("Save Project"), this, &QgisDesktopWindow::saveProject, QKeySequence::Save);
    projectMenu->addAction(tr("Save Project As..."), this, &QgisDesktopWindow::saveProjectAs);
    projectMenu->addSeparator();
    projectMenu->addAction(QIcon(":/icons/i_ort"), tr("Import Layer..."), this, &QgisDesktopWindow::importLayer);
    projectMenu->addSeparator();
    projectMenu->addAction(tr("Quit"), this, &QMainWindow::close, QKeySequence::Quit);

    // Edit Menu
    QMenu *editMenu = menuBar()->addMenu(tr("&Edit"));
    editMenu->addAction(tr("Undo"), this, &QgisDesktopWindow::undo, QKeySequence::Undo);
    editMenu->addAction(tr("Redo"), this, &QgisDesktopWindow::redo, QKeySequence::Redo);
    editMenu->addSeparator();
    editMenu->addAction(tr("Cut Features"), this, &QgisDesktopWindow::cutFeatures, QKeySequence::Cut);
    editMenu->addAction(tr("Copy Features"), this, &QgisDesktopWindow::copyFeatures, QKeySequence::Copy);
    editMenu->addAction(tr("Paste Features"), this, &QgisDesktopWindow::pasteFeatures, QKeySequence::Paste);
    editMenu->addSeparator();
    editMenu->addAction(tr("Select All"), this, &QgisDesktopWindow::selectAll, QKeySequence("Ctrl+A"));

    // View Menu
    QMenu *viewMenu = menuBar()->addMenu(tr("&View"));
    viewMenu->addAction(QIcon(":/icons/zoo_in"), tr("Zoom In"), this, &QgisDesktopWindow::zoomIn, QKeySequence::ZoomIn);
    viewMenu->addAction(QIcon(":/icons/zoo_out"), tr("Zoom Out"), this, &QgisDesktopWindow::zoomOut, QKeySequence::ZoomOut);
    viewMenu->addAction(QIcon(":/icons/full_extent"), tr("Zoom Full"), this, &QgisDesktopWindow::zoomFullExtent, QKeySequence("Ctrl+Shift+F"));
    viewMenu->addAction(tr("Zoom to Layer"), this, &QgisDesktopWindow::zoomToLayer, QKeySequence("Ctrl+L"));
    viewMenu->addSeparator();
    viewMenu->addAction(QIcon(":/icons/p_n"), tr("Pan"), this, &QgisDesktopWindow::panMap, QKeySequence("Space"));
    viewMenu->addAction(QIcon(":/icons/identify"), tr("Identify"), this, &QgisDesktopWindow::identifyFeatures, QKeySequence("Ctrl+Shift+I"));
    viewMenu->addSeparator();
    viewMenu->addAction(QIcon(":/icons/me_sure_dist"), tr("Measure Distance"), this, &QgisDesktopWindow::measureDistance, QKeySequence("Ctrl+Shift+D"));
    viewMenu->addAction(QIcon(":/icons/me_sure_are_"), tr("Measure Area"), this, &QgisDesktopWindow::measureArea, QKeySequence("Ctrl+Shift+A"));
    viewMenu->addSeparator();
    viewMenu->addAction(QIcon(":/icons/refresh_view"), tr("Refresh"), this, &QgisDesktopWindow::refreshMap, QKeySequence("F5"));

    // Layer Menu
    QMenu *layerMenu = menuBar()->addMenu(tr("&Layer"));
    layerMenu->addAction(QIcon(":/icons/r_ster"), tr("Add Raster Layer..."), this, &QgisDesktopWindow::addRasterLayer);
    layerMenu->addAction(QIcon(":/icons/vector"), tr("Add Vector Layer..."), this, &QgisDesktopWindow::addVectorLayer);
    layerMenu->addSeparator();
    layerMenu->addAction(tr("Layer Properties..."), this, &QgisDesktopWindow::layerProperties, QKeySequence("Ctrl+I"));
    layerMenu->addAction(tr("Remove Layer"), this, &QgisDesktopWindow::removeLayer, QKeySequence::Delete);
    layerMenu->addSeparator();
    layerMenu->addAction(tr("Set Project CRS..."), this, &QgisDesktopWindow::setProjectCrs);

    // Processing Menu
    QMenu *processingMenu = menuBar()->addMenu(tr("&Processing"));
    processingMenu->addAction(tr("Toolbox"), this, &QgisDesktopWindow::showProcessingToolbox);
    processingMenu->addSeparator();
    processingMenu->addAction(tr("History"), this, &QgisDesktopWindow::showProcessingHistory);

    // Raster Menu
    QMenu *rasterMenu = menuBar()->addMenu(tr("&Raster"));
    rasterMenu->addAction(QIcon(":/icons/r_ster_calc"), tr("Raster Calculator..."), this, [](){});
    rasterMenu->addAction(QIcon(":/icons/b_nd_m_th"), tr("Band Math..."), this, &QgisDesktopWindow::openBandMathDialog);
    rasterMenu->addAction(QIcon(":/icons/at_os_corr"), tr("Atmospheric Correction..."), this, &QgisDesktopWindow::openAtmosphericCorrectionDialog);
    rasterMenu->addAction(QIcon(":/icons/veget_tion_index"), tr("Vegetation Index..."), this, &QgisDesktopWindow::openSpectralIndexDialog);
    rasterMenu->addAction(QIcon(":/icons/mos_ic"), tr("Mosaic..."), this, [](){});
    rasterMenu->addSeparator();
    rasterMenu->addAction(QIcon(":/icons/r_ster_calc"), tr("Georeferencer..."), this, &QgisDesktopWindow::openGeoreferencer);
    // Phase 10A Task 10.2 — Classification submenu (Pixel-based + OBIA placeholder).
    auto *classifyMenu = rasterMenu->addMenu(tr("Classification"));
#ifdef SICNU_HAS_CLASSIFY
    classifyMenu->addAction(tr("Supervised Classification (Pixel-based)..."),
                            this, &QgisDesktopWindow::openClassificationWindow);
    auto *obiaAct = classifyMenu->addAction(tr("Object-based Classification (OBIA) — Phase 10B"));
    obiaAct->setEnabled(false);
#else
    auto *disabledAct = classifyMenu->addAction(tr("Classification (OpenCV ml unavailable)"));
    disabledAct->setEnabled(false);
#endif
    rasterMenu->addSeparator();
    rasterMenu->addAction(QIcon(":/icons/extr_ct_b_nd"), tr("Extract Band..."), this, [](){});
    rasterMenu->addAction(QIcon(":/icons/b_nd_co_bo"), tr("Band Composite..."), this, [](){});
    rasterMenu->addSeparator();
    auto *enhanceMenu = rasterMenu->addMenu(tr("Enhancement"));
    enhanceMenu->addAction(tr("Contrast Stretch..."), this, &QgisDesktopWindow::openContrastStretchDialog);
    enhanceMenu->addAction(tr("Spatial Filter..."), this, &QgisDesktopWindow::openSpatialFilterDialog);
    enhanceMenu->addAction(tr("PCA..."), this, &QgisDesktopWindow::openPcaDialog);
    enhanceMenu->addAction(tr("Band Ratio / IHS..."), this, &QgisDesktopWindow::openBandRatioDialog);

    // Vector Menu
    QMenu *vectorMenu = menuBar()->addMenu(tr("&Vector"));
    vectorMenu->addAction(QIcon(":/icons/buffer"), tr("Buffer..."), this, [this](){ openProcessingAlgorithm("vector_buffer"); });
    vectorMenu->addAction(QIcon(":/icons/dissolve"), tr("Dissolve..."), this, [this](){ openProcessingAlgorithm("vector_dissolve"); });
    vectorMenu->addAction(QIcon(":/icons/merge"), tr("Merge..."), this, [this](){ openProcessingAlgorithm("vector_merge"); });
    vectorMenu->addAction(QIcon(":/icons/cli_"), tr("Clip..."), this, [this](){ openProcessingAlgorithm("vector_clip"); });

    // Settings Menu
    QMenu *settingsMenu = menuBar()->addMenu(tr("&Settings"));
    settingsMenu->addAction(QIcon(":/icons/settings"), tr("Options..."), this, &QgisDesktopWindow::options);
    settingsMenu->addSeparator();
    settingsMenu->addAction(QIcon(":/icons/define_crs"), tr("CRS Presets..."), this, &QgisDesktopWindow::openCrsPresetDialog);

    // Database Menu (placeholder)
    QMenu *databaseMenu = menuBar()->addMenu(tr("&Database"));
    databaseMenu->addAction(tr("Connect..."), this, [](){});
    databaseMenu->addAction(tr("Import from Database..."), this, [](){});
    databaseMenu->addAction(tr("Export to Database..."), this, [](){});

    // AI Assistant Menu (placeholder)
    QMenu *aiMenu = menuBar()->addMenu(tr("&AI Assistant"));
    aiMenu->addAction(QIcon(":/icons/dee_le_rning"), tr("Classification Assistant"), this, [](){});
    aiMenu->addAction(QIcon(":/icons/object_detect"), tr("Object Detection"), this, [](){});
    aiMenu->addAction(QIcon(":/icons/seg_ent_tion"), tr("Image Segmentation"), this, [](){});

    // Window Menu (dock toggle actions added in setupDockWidgets)
    m_windowMenu = menuBar()->addMenu(tr("&Window"));

    // Help Menu
    QMenu *helpMenu = menuBar()->addMenu(tr("&Help"));
    helpMenu->addAction(QIcon(":/icons/hel_"), tr("Help Contents"), this, &QgisDesktopWindow::helpContents, QKeySequence::HelpContents);
    helpMenu->addSeparator();
    helpMenu->addAction(tr("Check Version"), this, &QgisDesktopWindow::checkVersion);
    helpMenu->addAction(tr("About"), this, &QgisDesktopWindow::about);
}

void QgisDesktopWindow::setupToolbars()
{
    // File Toolbar
    QToolBar *fileToolBar = addToolBar("File");
    fileToolBar->setObjectName("fileToolBar");
    fileToolBar->setIconSize(QSize(24, 24));
    fileToolBar->setToolButtonStyle(Qt::ToolButtonIconOnly);

    fileToolBar->addAction(QIcon(":/icons/new_project"), tr("New Project"), this, &QgisDesktopWindow::newProject)->setToolTip(tr("New Project (Ctrl+N)"));
    fileToolBar->addAction(QIcon(":/icons/o_en"), tr("Open Project"), this, &QgisDesktopWindow::openProject)->setToolTip(tr("Open Project (Ctrl+O)"));
    fileToolBar->addAction(QIcon(":/icons/s_ve"), tr("Save Project"), this, &QgisDesktopWindow::saveProject)->setToolTip(tr("Save Project (Ctrl+S)"));
    fileToolBar->addSeparator();
    fileToolBar->addAction(QIcon(":/icons/r_ster"), tr("Add Raster Layer"), this, &QgisDesktopWindow::addRasterLayer)->setToolTip(tr("Add Raster Layer"));
    fileToolBar->addAction(QIcon(":/icons/vector"), tr("Add Vector Layer"), this, &QgisDesktopWindow::addVectorLayer)->setToolTip(tr("Add Vector Layer"));

    // Map Tools Toolbar
    QToolBar *mapToolsToolBar = addToolBar("Map Tools");
    mapToolsToolBar->setObjectName("mapToolsToolBar");
    mapToolsToolBar->setIconSize(QSize(24, 24));
    mapToolsToolBar->setToolButtonStyle(Qt::ToolButtonIconOnly);

    mapToolsToolBar->addAction(QIcon(":/icons/p_n"), tr("Pan"), this, &QgisDesktopWindow::panMap)->setToolTip(tr("Pan (Space)"));
    mapToolsToolBar->addAction(QIcon(":/icons/zoo_in"), tr("Zoom In"), this, &QgisDesktopWindow::zoomIn)->setToolTip(tr("Zoom In (Ctrl+Plus)"));
    mapToolsToolBar->addAction(QIcon(":/icons/zoo_out"), tr("Zoom Out"), this, &QgisDesktopWindow::zoomOut)->setToolTip(tr("Zoom Out (Ctrl+Minus)"));
    mapToolsToolBar->addAction(QIcon(":/icons/full_extent"), tr("Full Extent"), this, &QgisDesktopWindow::zoomFullExtent)->setToolTip(tr("Zoom Full Extent (Ctrl+Shift+F)"));
    mapToolsToolBar->addSeparator();
    mapToolsToolBar->addAction(QIcon(":/icons/identify"), tr("Identify"), this, &QgisDesktopWindow::identifyFeatures)->setToolTip(tr("Identify Features (Ctrl+Shift+I)"));
    mapToolsToolBar->addSeparator();
    mapToolsToolBar->addAction(QIcon(":/icons/me_sure_dist"), tr("Measure Distance"), this, &QgisDesktopWindow::measureDistance)->setToolTip(tr("Measure Distance (Ctrl+Shift+D)"));
    mapToolsToolBar->addAction(QIcon(":/icons/me_sure_are_"), tr("Measure Area"), this, &QgisDesktopWindow::measureArea)->setToolTip(tr("Measure Area (Ctrl+Shift+A)"));

    // CRS Selector in toolbar
    m_crsSelector = new QgsProjectionSelectionWidget(mapToolsToolBar);
    m_crsSelector->setOptionVisible(QgsProjectionSelectionWidget::ProjectCrs, true);
    connect(m_crsSelector, &QgsProjectionSelectionWidget::crsChanged,
            this, &QgisDesktopWindow::onCrsChanged);
    mapToolsToolBar->addSeparator();
    mapToolsToolBar->addWidget(m_crsSelector);

    // Remote Sensing Toolbar
    QToolBar *rsToolBar = addToolBar("Remote Sensing");
    rsToolBar->setObjectName("rsToolBar");
    rsToolBar->setIconSize(QSize(24, 24));
    rsToolBar->setToolButtonStyle(Qt::ToolButtonIconOnly);

    rsToolBar->addAction(QIcon(":/icons/veget_tion_index"), tr("Vegetation Index"), this, [](){})->setToolTip(tr("Vegetation Index (NDVI, EVI, etc.)"));
    rsToolBar->addAction(QIcon(":/icons/b_nd_co_bo"), tr("Band Composition"), this, [](){})->setToolTip(tr("Band Composition"));
    rsToolBar->addAction(QIcon(":/icons/at_os_corr"), tr("Atmospheric Correction"), this, [](){})->setToolTip(tr("Atmospheric Correction (DOS1/DOS2)"));
    rsToolBar->addAction(QIcon(":/icons/mos_ic"), tr("Mosaic"), this, [](){})->setToolTip(tr("Mosaic / Stitching"));
    rsToolBar->addSeparator();
    rsToolBar->addAction(QIcon(":/icons/r_ster_calc"), tr("Raster Calculator"), this, [](){})->setToolTip(tr("Raster Calculator"));
    rsToolBar->addAction(QIcon(":/icons/su_ervised"), tr("Supervised Classification"), this, [](){})->setToolTip(tr("Supervised Classification"));
    rsToolBar->addAction(QIcon(":/icons/b_nd_m_th"), tr("Band Math"), this, [](){})->setToolTip(tr("Band Math Expression"));
}

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

    // Tabify the left dock widgets
    tabifyDockWidget(m_layersDock, m_browserDock);
    m_layersDock->raise();

    // Processing Toolbox Panel (Right, with Overview)
    m_processingDock = new QgsDockWidget("Processing Toolbox", this);
    m_processingDock->setObjectName("processingDock");
    m_processingDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    m_toolboxView = new QgsProcessingToolboxTreeView(m_processingDock);
    m_toolboxView->setRegistry(QgsApplication::processingRegistry());
    m_processingDock->setWidget(m_toolboxView);
    addDockWidget(Qt::RightDockWidgetArea, m_processingDock);

    // Python Console (disabled — Python runtime removed)
    // QgsDockWidget *pythonDock = new QgsDockWidget("Python Console", this);
    // pythonDock->setObjectName("pythonDock");
    // auto *pythonConsole = new PythonConsoleWidget(pythonDock);
    // pythonDock->setWidget(pythonConsole);
    // addDockWidget(Qt::BottomDockWidgetArea, pythonDock);

    // Double-click on algorithm in toolbox opens execution dialog
    connect(m_toolboxView, &QgsProcessingToolboxTreeView::doubleClicked, this, [this](const QModelIndex &index) {
        const QgsProcessingAlgorithm *alg = m_toolboxView->algorithmForIndex(index);
        if (!alg)
            return;

        openProcessingAlgorithm(alg->id());
    });

    // Overview Panel (Right, tabified with Processing Toolbox)
    // Widget is set later in setupMapCanvas() once m_mapCanvas exists
    m_overviewDock = new QgsDockWidget("Overview", this);
    m_overviewDock->setObjectName("overviewDock");
    m_overviewDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
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

    // Log Panel (Bottom, tabified)
    m_logDock = new LogPanel(this);
    m_logDock->setObjectName("logDock");
    addDockWidget(Qt::BottomDockWidgetArea, m_logDock);

    // Window menu — add dock toggle actions
    if (m_windowMenu) {
        m_windowMenu->addSeparator();
        m_windowMenu->addAction(m_layersDock->toggleViewAction());
        m_windowMenu->addAction(m_browserDock->toggleViewAction());
        m_windowMenu->addAction(m_processingDock->toggleViewAction());
        m_windowMenu->addAction(m_overviewDock->toggleViewAction());
        m_windowMenu->addAction(m_identifyDock->toggleViewAction());
        m_windowMenu->addAction(m_spectralDock->toggleViewAction());
        m_windowMenu->addAction(m_logDock->toggleViewAction());
        m_windowMenu->addSeparator();
        QAction *resetLayoutAction = m_windowMenu->addAction(tr("Reset Layout"));
        connect(resetLayoutAction, &QAction::triggered, this, &QgisDesktopWindow::resetPanelLayout);
    }
}

void QgisDesktopWindow::setupStatusBar()
{
    QStatusBar *bar = statusBar();
    bar->setObjectName("rsStatusBar");
    bar->setFixedHeight(22);

    // Ready status (left side)
    QLabel *readyLabel = new QLabel("Ready", bar);
    readyLabel->setObjectName("rsSegOk");
    bar->addWidget(readyLabel);

    // Coordinates display
    m_coordinatesLabel = new QLabel("0.000000, 0.000000", bar);
    m_coordinatesLabel->setObjectName("rsCoordLabel");
    bar->addPermanentWidget(m_coordinatesLabel);

    // Scale display
    m_scaleLabel = new QLabel("Scale: 1:1,000", bar);
    m_scaleLabel->setObjectName("rsScaleLabel");
    bar->addPermanentWidget(m_scaleLabel);

    // CRS display
    m_crsLabel = new QLabel("EPSG:3857", bar);
    m_crsLabel->setObjectName("rsCrsLabel");
    bar->addPermanentWidget(m_crsLabel);

    // Render time display
    m_renderTimeLabel = new QLabel("", bar);
    m_renderTimeLabel->setObjectName("rsRenderLabel");
    bar->addPermanentWidget(m_renderTimeLabel);

    // Cache usage
    m_cacheLabel = new QLabel("Cache: 0 MB", bar);
    m_cacheLabel->setObjectName("rsCacheLabel");
    bar->addPermanentWidget(m_cacheLabel);
}

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
    QgsProject *project = QgsProject::instance();
    QgsLayerTree *root = project->layerTreeRoot();

    // Create layer tree model with QGIS-compatible flags
    m_layerTreeModel = new QgsLayerTreeModel(root, this);

    // Display flags (matching QGIS defaults)
    m_layerTreeModel->setFlag(QgsLayerTreeModel::ShowLegend);
    m_layerTreeModel->setFlag(QgsLayerTreeModel::ShowLegendAsTree);
    m_layerTreeModel->setFlag(QgsLayerTreeModel::UseEmbeddedWidgets);
    m_layerTreeModel->setFlag(QgsLayerTreeModel::UseTextFormatting);

    // Behavioral flags (matching QGIS defaults)
    m_layerTreeModel->setFlag(QgsLayerTreeModel::AllowNodeReorder);
    m_layerTreeModel->setFlag(QgsLayerTreeModel::AllowNodeRename);
    m_layerTreeModel->setFlag(QgsLayerTreeModel::AllowNodeChangeVisibility);
    m_layerTreeModel->setFlag(QgsLayerTreeModel::AllowLegendChangeState);
    m_layerTreeModel->setFlag(QgsLayerTreeModel::ActionHierarchical);

    m_layerTreeView->setLayerTreeModel(m_layerTreeModel);
    m_layerTreeView->setModel(m_layerTreeModel);

    // Expand all nodes by default (QGIS behavior)
    m_layerTreeView->expandAll();

    // Connect layer tree signals
    connect(m_layerTreeView, &QgsLayerTreeView::clicked,
            this, &QgisDesktopWindow::onLayerTreeClicked);
    connect(m_layerTreeView, &QgsLayerTreeView::doubleClicked,
            this, &QgisDesktopWindow::onLayerTreeDoubleClicked);

    // Connect project signals for CRS updates
    connect(project, &QgsProject::crsChanged,
            this, &QgisDesktopWindow::updateCrsDisplay);

    // Bridge: automatic layer tree → canvas synchronization
    m_layerTreeBridge = new QgsLayerTreeMapCanvasBridge(root, m_mapCanvas, this);
    connect(m_layerTreeBridge, &QgsLayerTreeMapCanvasBridge::canvasLayersChanged,
            this, [this]() {
                if (m_overviewCanvas)
                    m_overviewCanvas->setLayers(m_mapCanvas->layers());
            });

    // Set up native QGIS context menu for layer tree
    m_layerTreeMenuProvider = new LayerTreeMenuProvider(m_layerTreeView, m_mapCanvas, this);
    m_layerTreeView->setMenuProvider(m_layerTreeMenuProvider);
}

// ── Project Actions ────────────────────────────────────────────────────────
void QgisDesktopWindow::newProject()
{
    QgsProject::instance()->clear();
    m_mapCanvas->setLayers({});
    m_mapCanvas->refresh();
    statusBar()->showMessage("New project created", 3000);
}

void QgisDesktopWindow::openProject()
{
    QString filePath = QFileDialog::getOpenFileName(
        this, "Open Project", "",
        "QGIS Projects (*.qgs *.qgz);;All Files (*.*)"
    );
    if (!filePath.isEmpty()) {
        QgsProject::instance()->read(filePath);
        refreshCanvasLayers();
        statusBar()->showMessage(QString("Opened project: %1").arg(filePath), 3000);
    }
}

void QgisDesktopWindow::saveProject()
{
    if (QgsProject::instance()->fileName().isEmpty()) {
        saveProjectAs();
    } else {
        QgsProject::instance()->write();
        statusBar()->showMessage("Project saved", 3000);
    }
}

void QgisDesktopWindow::saveProjectAs()
{
    QString filePath = QFileDialog::getSaveFileName(
        this, "Save Project", "",
        "QGIS Projects (*.qgs);;All Files (*.*)"
    );
    if (!filePath.isEmpty()) {
        QgsProject::instance()->write(filePath);
        statusBar()->showMessage(QString("Saved project: %1").arg(filePath), 3000);
    }
}

void QgisDesktopWindow::importLayer() { addRasterLayer(); }

// ── Edit Actions ──────────────────────────────────────────────────────────
void QgisDesktopWindow::undo() { statusBar()->showMessage("Undo", 2000); }
void QgisDesktopWindow::redo() { statusBar()->showMessage("Redo", 2000); }
void QgisDesktopWindow::cutFeatures() { statusBar()->showMessage("Cut features", 2000); }
void QgisDesktopWindow::copyFeatures() { statusBar()->showMessage("Copy features", 2000); }
void QgisDesktopWindow::pasteFeatures() { statusBar()->showMessage("Paste features", 2000); }
void QgisDesktopWindow::selectAll() { statusBar()->showMessage("Select all", 2000); }

// ── View Actions ──────────────────────────────────────────────────────────
void QgisDesktopWindow::zoomIn() { m_mapCanvas->zoomIn(); }
void QgisDesktopWindow::zoomOut() { m_mapCanvas->zoomOut(); }
void QgisDesktopWindow::panMap() { m_mapCanvas->setMapTool(m_panTool); }
void QgisDesktopWindow::identifyFeatures() { m_mapCanvas->setMapTool(m_identifyTool); }

void QgisDesktopWindow::measureDistance()
{
    m_mapCanvas->setMapTool( m_measureDistanceTool );
    statusBar()->showMessage( tr( "Measure Distance: click to add points, double-click or right-click to finish" ), 5000 );
}

void QgisDesktopWindow::measureArea()
{
    m_mapCanvas->setMapTool( m_measureAreaTool );
    statusBar()->showMessage( tr( "Measure Area: click to add points, double-click or right-click to finish" ), 5000 );
}

void QgisDesktopWindow::openGeoreferencer()
{
    if ( !m_georefWindow )
    {
        // iface = nullptr for now; Task 11.4.7 will pass a real QgisInterface.
        m_georefWindow = new QgsGeoreferencerMainWindow( nullptr, this );
        m_georefWindow->setAttribute( Qt::WA_DeleteOnClose, false );
    }
    m_georefWindow->show();
    m_georefWindow->raise();
    m_georefWindow->activateWindow();
}

#ifdef SICNU_HAS_CLASSIFY
void QgisDesktopWindow::openClassificationWindow()
{
    if ( !m_classifyWindow )
    {
        // iface = nullptr for now; later tasks may pass a real QgisInterface.
        m_classifyWindow = new QgsClassificationMainWindow( nullptr, this );
        m_classifyWindow->setAttribute( Qt::WA_DeleteOnClose, false );
    }
    m_classifyWindow->show();
    m_classifyWindow->raise();
    m_classifyWindow->activateWindow();
}
#else
void QgisDesktopWindow::openClassificationWindow() {}
#endif

void QgisDesktopWindow::onIdentifyResults(const QList<QgsMapToolIdentify::IdentifyResult> &results)
{
    if (results.isEmpty())
    {
        m_identifyResults->setHtml(QStringLiteral(
            "<html><body style='font-family:sans-serif; padding:8px;'>"
            "<p style='color:#888;'>%1</p>"
            "</body></html>"
        ).arg(tr("No features found at this location.")));
        if (m_identifyDock)
            m_identifyDock->raise();
        return;
    }

    QString html;
    html.reserve(4096);
    html += QStringLiteral(
        "<html><body style='font-family:sans-serif; padding:4px;'>"
    );

    for (const QgsMapToolIdentify::IdentifyResult &result : results)
    {
        // Layer name
        const QString layerName = result.mLayer
            ? result.mLayer->name()
            : tr("Unknown Layer");

        html += QStringLiteral("<h3 style='margin-bottom:2px;'>%1</h3>").arg(layerName.toHtmlEscaped());

        // Determine if this is a raster or vector result
        const bool isRaster = result.mLayer
            && result.mLayer->type() == Qgis::LayerType::Raster;

        html += QStringLiteral(
            "<table style='border-collapse:collapse; width:100%;'>"
        );

        if (isRaster)
        {
            // Raster: show label (pixel value / band info) and attributes
            if (!result.mLabel.isEmpty())
            {
                html += QStringLiteral(
                    "<tr style='border-bottom:1px solid #ddd;'>"
                    "<td style='padding:3px 8px; font-weight:bold; white-space:nowrap;'>%1</td>"
                    "<td style='padding:3px 8px;'>%2</td>"
                    "</tr>"
                ).arg(tr("Value"), result.mLabel.toHtmlEscaped());
            }

            // Raster attributes (band values)
            for (auto it = result.mAttributes.constBegin(); it != result.mAttributes.constEnd(); ++it)
            {
                html += QStringLiteral(
                    "<tr style='border-bottom:1px solid #ddd;'>"
                    "<td style='padding:3px 8px; font-weight:bold; white-space:nowrap;'>%1</td>"
                    "<td style='padding:3px 8px;'>%2</td>"
                    "</tr>"
                ).arg(it.key().toHtmlEscaped(), it.value().toHtmlEscaped());
            }

            // Derived attributes (coordinates, etc.)
            for (auto it = result.mDerivedAttributes.constBegin(); it != result.mDerivedAttributes.constEnd(); ++it)
            {
                html += QStringLiteral(
                    "<tr style='border-bottom:1px solid #ddd;'>"
                    "<td style='padding:3px 8px; font-weight:bold; white-space:nowrap;'>%1</td>"
                    "<td style='padding:3px 8px;'>%2</td>"
                    "</tr>"
                ).arg(it.key().toHtmlEscaped(), it.value().toHtmlEscaped());
            }
        }
        else
        {
            // Vector: show feature attributes from fields
            const QgsFields fields = result.mFields;
            const QgsFeature feature = result.mFeature;

            if (fields.isEmpty() && !result.mAttributes.isEmpty())
            {
                // Fallback: use mAttributes map (e.g. for vector tile layers)
                for (auto it = result.mAttributes.constBegin(); it != result.mAttributes.constEnd(); ++it)
                {
                    html += QStringLiteral(
                        "<tr style='border-bottom:1px solid #ddd;'>"
                        "<td style='padding:3px 8px; font-weight:bold; white-space:nowrap;'>%1</td>"
                        "<td style='padding:3px 8px;'>%2</td>"
                        "</tr>"
                    ).arg(it.key().toHtmlEscaped(), it.value().toHtmlEscaped());
                }
            }
            else if (!fields.isEmpty())
            {
                // Show field name + value pairs from the feature
                for (int i = 0; i < fields.count(); ++i)
                {
                    const QString fieldName = fields.at(i).name();
                    const QString fieldValue = feature.attribute(i).toString();
                    html += QStringLiteral(
                        "<tr style='border-bottom:1px solid #ddd;'>"
                        "<td style='padding:3px 8px; font-weight:bold; white-space:nowrap;'>%1</td>"
                        "<td style='padding:3px 8px;'>%2</td>"
                        "</tr>"
                    ).arg(fieldName.toHtmlEscaped(), fieldValue.toHtmlEscaped());
                }
            }

            // Derived attributes (coordinates, area, etc.)
            for (auto it = result.mDerivedAttributes.constBegin(); it != result.mDerivedAttributes.constEnd(); ++it)
            {
                html += QStringLiteral(
                    "<tr style='border-bottom:1px solid #ddd;'>"
                    "<td style='padding:3px 8px; font-weight:bold; white-space:nowrap;'>%1</td>"
                    "<td style='padding:3px 8px;'>%2</td>"
                    "</tr>"
                ).arg(it.key().toHtmlEscaped(), it.value().toHtmlEscaped());
            }
        }

        html += QStringLiteral("</table><br/>");
    }

    html += QStringLiteral("</body></html>");

    m_identifyResults->setHtml(html);

    // Raise the dock so the user can see results
    if (m_identifyDock)
        m_identifyDock->raise();

    // Update spectral profile for the first raster layer result
    if (m_spectralProfile)
    {
        QgsRasterLayer *rasterLayer = nullptr;
        for (const QgsMapToolIdentify::IdentifyResult &result : results)
        {
            if (result.mLayer && result.mLayer->type() == Qgis::LayerType::Raster)
            {
                rasterLayer = qobject_cast<QgsRasterLayer *>(result.mLayer);
                if (rasterLayer)
                    break;
            }
        }

        if (rasterLayer && m_identifyTool)
        {
            QgsPointXY clickedPoint = m_identifyTool->lastClickedPoint();
            m_spectralProfile->setProfile(clickedPoint, rasterLayer);
        }
    }
}

void QgisDesktopWindow::zoomFullExtent()
{
    m_mapCanvas->zoomToFullExtent();
    statusBar()->showMessage("Full extent", 2000);
}

void QgisDesktopWindow::zoomToLayer()
{
    QList<QgsMapLayer*> selected = selectedLayers();
    if (!selected.isEmpty()) {
        m_mapCanvas->setExtent(selected.first()->extent());
        m_mapCanvas->refresh();
        statusBar()->showMessage("Zoomed to layer", 2000);
    }
}

void QgisDesktopWindow::refreshMap()
{
    m_mapCanvas->refresh();
    statusBar()->showMessage("Map refreshed", 2000);
}

// ── Layer Actions (public for LayerTreeMenuProvider) ──────────────────────
void QgisDesktopWindow::addRasterLayer()
{
    QString filePath = QFileDialog::getOpenFileName(
        this, "Open Raster Layer",
        AppPaths::dataDir(),
        "Raster Files (*.tif *.tiff *.img *.jp2 *.png *.jpg *.jpeg);;All Files (*.*)"
    );
    if (!filePath.isEmpty()) {
        loadRasterLayer(filePath);
    }
}

void QgisDesktopWindow::addVectorLayer()
{
    QString filePath = QFileDialog::getOpenFileName(
        this, "Open Vector Layer",
        AppPaths::dataDir(),
        "Vector Files (*.shp *.gpkg *.geojson *.kml *.gml);;All Files (*.*)"
    );
    if (!filePath.isEmpty()) {
        loadVectorLayer(filePath);
    }
}

void QgisDesktopWindow::layerProperties()
{
    QList<QgsMapLayer*> selected = selectedLayers();
    if (selected.isEmpty()) {
        QMessageBox::information(this, "Layer Properties", "No layer selected");
        return;
    }

    QgsMapLayer *layer = selected.first();
    showLayerProperties(layer);
}

void QgisDesktopWindow::removeLayer()
{
    QList<QgsMapLayer*> selected = selectedLayers();
    if (selected.isEmpty()) {
        statusBar()->showMessage("No layer selected", 2000);
        return;
    }

    for (QgsMapLayer *layer : selected) {
        QgsProject::instance()->removeMapLayer(layer->id());
    }
    refreshCanvasLayers();
    statusBar()->showMessage("Layer removed", 2000);
}

void QgisDesktopWindow::setProjectCrs()
{
    // Use simple input dialog instead of QgsProjectionSelectionDialog
    QStringList crsOptions;
    crsOptions << "EPSG:4326 (WGS 84)"
               << "EPSG:3857 (Web Mercator)"
               << "EPSG:32649 (UTM 49N)"
               << "EPSG:32650 (UTM 50N)"
               << "EPSG:32651 (UTM 51N)";

    bool ok;
    QString item = QInputDialog::getItem(this, "Set Project CRS",
        "Select CRS:", crsOptions, 0, false, &ok);
    if (ok && !item.isEmpty()) {
        // Extract EPSG code
        QString epsgCode = item.left(item.indexOf(' '));
        QgsCoordinateReferenceSystem crs(epsgCode);
        if (crs.isValid()) {
            QgsProject::instance()->setCrs(crs);
            m_mapCanvas->setDestinationCrs(crs);
            m_mapCanvas->refresh();
            updateCrsDisplay();
            statusBar()->showMessage(QString("Project CRS set to: %1").arg(crs.authid()), 3000);
        }
    }
}

// ── Settings Actions ──────────────────────────────────────────────────────
void QgisDesktopWindow::options()
{
    PreferencesDialog dialog(this);
    if (dialog.exec() == QDialog::Accepted)
    {
        // Apply theme if changed
        QString theme = dialog.theme();
        if (theme == "dark")
        {
            // Apply dark theme
            qApp->setStyle(QStyleFactory::create("Fusion"));
            QPalette darkPalette;
            darkPalette.setColor(QPalette::Window, QColor(53, 53, 53));
            darkPalette.setColor(QPalette::WindowText, Qt::white);
            darkPalette.setColor(QPalette::Base, QColor(25, 25, 25));
            darkPalette.setColor(QPalette::AlternateBase, QColor(53, 53, 53));
            darkPalette.setColor(QPalette::ToolTipBase, Qt::white);
            darkPalette.setColor(QPalette::ToolTipText, Qt::white);
            darkPalette.setColor(QPalette::Text, Qt::white);
            darkPalette.setColor(QPalette::Button, QColor(53, 53, 53));
            darkPalette.setColor(QPalette::ButtonText, Qt::white);
            darkPalette.setColor(QPalette::BrightText, Qt::red);
            darkPalette.setColor(QPalette::Link, QColor(42, 130, 218));
            darkPalette.setColor(QPalette::Highlight, QColor(42, 130, 218));
            darkPalette.setColor(QPalette::HighlightedText, Qt::black);
            qApp->setPalette(darkPalette);
        }
        else
        {
            // Reset to default light theme
            qApp->setPalette(QApplication::style()->standardPalette());
        }

        statusBar()->showMessage(tr("Preferences saved"), 3000);
    }
}

// ── Processing Actions ─────────────────────────────────────────────────────
void QgisDesktopWindow::showProcessingToolbox()
{
    // Find and raise the processing dock
    for (QDockWidget *dock : findChildren<QDockWidget*>()) {
        if (dock->objectName() == "processingDock") {
            dock->show();
            dock->raise();
            break;
        }
    }
}

void QgisDesktopWindow::showProcessingHistory()
{
    QMessageBox::information(this, "Processing History", "Processing history coming soon...");
}

// ── Help Actions ──────────────────────────────────────────────────────────
void QgisDesktopWindow::helpContents() { QMessageBox::information(this, "Help", "QGIS Help"); }
void QgisDesktopWindow::checkVersion() { QMessageBox::information(this, "Version", "SICNU GEO RS v1.0"); }
void QgisDesktopWindow::about()
{
    QMessageBox::about(this, "About",
        "SICNU GEO RS\n\n"
        "Professional Remote Sensing Analysis Platform\n"
        "Built with QGIS C++ Libraries\n\n"
        "Version 1.0\n\n"
        "Features:\n"
        "- Raster and vector layer support\n"
        "- QGIS-compatible layer properties\n"
        "- CRS/Projection selection\n"
        "- Native QGIS rendering performance");
}

// ── Coordinate and Scale Updates ───────────────────────────────────────────
void QgisDesktopWindow::showCoordinates(const QgsPointXY &point)
{
    m_coordinatesLabel->setText(QString("%1, %2")
        .arg(point.x(), 0, 'f', 2)
        .arg(point.y(), 0, 'f', 2));
}

void QgisDesktopWindow::updateScale()
{
    double s = m_mapCanvas->scale();
    m_scaleLabel->setText(QString("Scale: 1:%1").arg(static_cast<int>(s)));
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
    m_crsLabel->setText(crs.authid());
    if (m_crsSelector) {
        m_crsSelector->setCrs(crs);
    }
}

void QgisDesktopWindow::onRenderComplete(QPainter *painter)
{
    Q_UNUSED(painter);
    // Render time is tracked internally by QGIS
}

void QgisDesktopWindow::onProjectRead(const QDomDocument &doc)
{
    Q_UNUSED(doc);
    refreshCanvasLayers();
    updateCrsDisplay();
    statusBar()->showMessage("Project loaded", 3000);
}

void QgisDesktopWindow::onProjectWrite(QDomDocument &doc)
{
    Q_UNUSED(doc);
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
    Q_UNUSED(index);
    // Don't refresh on every click - only on selection change
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

QgsLayerTreeGroup *QgisDesktopWindow::findOrCreateGroup(const QString &name)
{
    QgsLayerTree *root = QgsProject::instance()->layerTreeRoot();
    QgsLayerTreeGroup *group = root->findGroup(name);
    if (!group) {
        group = root->addGroup(name);
    }
    return group;
}

// ── Layer Loading ─────────────────────────────────────────────────────────
void QgisDesktopWindow::loadRasterLayer(const QString &filePath)
{
    QFileInfo fi(filePath);
    QString name = fi.fileName();

    QgsRasterLayer *layer = new QgsRasterLayer(filePath, name, "gdal");

    if (!layer->isValid()) {
        QMessageBox::warning(this, "Load Layer",
            QString("Failed to load raster layer:\n%1\n\nError: %2")
                .arg(filePath, layer->error().message()));
        delete layer;
        return;
    }

    QgsProject::instance()->addMapLayer(layer, /*addToLegend=*/false);

    QgsLayerTreeGroup *group = findOrCreateGroup("Raster Layers");
    group->addLayer(layer);

    // Zoom to layer extent first, then update layers (single refresh)
    m_mapCanvas->setExtent(layer->extent());
    refreshCanvasLayers();

    statusBar()->showMessage(QString("Loaded: %1 (%2x%3, %4 bands)")
        .arg(name)
        .arg(layer->width())
        .arg(layer->height())
        .arg(layer->bandCount()), 3000);
}

void QgisDesktopWindow::loadVectorLayer(const QString &filePath)
{
    QFileInfo fi(filePath);
    QString name = fi.fileName();

    QgsVectorLayer *layer = new QgsVectorLayer(filePath, name, "ogr");

    if (!layer->isValid()) {
        QMessageBox::warning(this, "Load Layer",
            QString("Failed to load vector layer:\n%1\n\nError: %2")
                .arg(filePath, layer->error().message()));
        delete layer;
        return;
    }

    QgsProject::instance()->addMapLayer(layer, /*addToLegend=*/false);

    QgsLayerTreeGroup *group = findOrCreateGroup("Vector Layers");
    group->addLayer(layer);

    // Zoom to layer extent first, then update layers (single refresh)
    m_mapCanvas->setExtent(layer->extent());
    refreshCanvasLayers();

    statusBar()->showMessage(QString("Loaded: %1 (%2 features)")
        .arg(name)
        .arg(layer->featureCount()), 3000);
}

void QgisDesktopWindow::showLayerProperties(QgsMapLayer *layer)
{
    if (!layer) return;

    if (layer->type() == Qgis::LayerType::Raster) {
        QgsRasterLayer *rasterLayer = qobject_cast<QgsRasterLayer*>(layer);
        if (rasterLayer) {
            QgsRasterLayerProperties dialog(rasterLayer, m_mapCanvas, this);
            dialog.exec();
            m_mapCanvas->refresh();
        }
    } else if (layer->type() == Qgis::LayerType::Vector) {
        QgsVectorLayer *vectorLayer = qobject_cast<QgsVectorLayer*>(layer);
        if (vectorLayer) {
            QgsVectorLayerProperties dialog(m_mapCanvas, nullptr, vectorLayer, this);
            dialog.exec();
            m_mapCanvas->refresh();
        }
    }
}

void QgisDesktopWindow::refreshCanvasLayers()
{
    QgsLayerTree *root = QgsProject::instance()->layerTreeRoot();
    QList<QgsMapLayer*> layers = root->layerOrder();
    m_mapCanvas->setLayers(layers);

    // Keep overview canvas in sync with the main canvas layers
    if (m_overviewCanvas)
    {
        m_overviewCanvas->setLayers(layers);
    }
}

QList<QgsMapLayer*> QgisDesktopWindow::selectedLayers()
{
    QList<QgsMapLayer*> result;
    QModelIndexList selected = m_layerTreeView->selectionModel()->selectedIndexes();
    for (const QModelIndex &idx : selected) {
        QgsLayerTreeNode *node = m_layerTreeView->index2node(idx);
        if (node && node->nodeType() == QgsLayerTreeNode::NodeLayer) {
            QgsLayerTreeLayer *layerNode = static_cast<QgsLayerTreeLayer*>(node);
            if (layerNode->layer()) {
                result.append(layerNode->layer());
            }
        }
    }
    return result;
}

void QgisDesktopWindow::openProcessingAlgorithm(const QString &algorithmId)
{
    const QgsProcessingAlgorithm *alg = QgsApplication::processingRegistry()->algorithmById(algorithmId);
    if (!alg)
    {
        QMessageBox::warning(this, tr("Algorithm Not Found"),
                             tr("Could not find processing algorithm: %1").arg(algorithmId));
        return;
    }

    QgsProcessingAlgorithm *algorithm = alg->create();
    if (!algorithm)
        return;

    // Concrete subclass implementing the pure virtual methods
    class SimpleAlgorithmDialog : public QgsProcessingAlgorithmDialogBase
    {
    public:
        using QgsProcessingAlgorithmDialogBase::QgsProcessingAlgorithmDialogBase;

        QVariantMap createProcessingParameters(Flags = Flags()) override
        {
            QVariantMap params;
            // Collect parameters from the parameter widgets
            for (auto it = mParamWidgets.begin(); it != mParamWidgets.end(); ++it) {
                params[it.key()] = it.value();
            }
            return params;
        }

        QgsProcessingContext *processingContext() override
        {
            return &mContext;
        }

        void setParameterValue(const QString &name, const QVariant &value)
        {
            mParamWidgets[name] = value;
        }

    private:
        QgsProcessingContext mContext;
        QVariantMap mParamWidgets;
    };

    SimpleAlgorithmDialog *dlg = new SimpleAlgorithmDialog(this);
    dlg->setAlgorithm(algorithm);

    // Create a simple parameter panel
    QWidget *paramPanel = new QWidget();
    QVBoxLayout *paramLayout = new QVBoxLayout(paramPanel);

    // Create input fields for each parameter
    QMap<QString, QWidget*> paramFieldMap;
    for (const QgsProcessingParameterDefinition *param : algorithm->parameterDefinitions()) {
        QHBoxLayout *rowLayout = new QHBoxLayout();
        QLabel *label = new QLabel(param->description());
        label->setToolTip(param->name());
        rowLayout->addWidget(label);

        // Create appropriate widget based on parameter type
        QWidget *fieldWidget = nullptr;
        if (dynamic_cast<const QgsProcessingParameterRasterLayer*>(param)) {
            // For raster layers, create a combo box with available layers
            QComboBox *combo = new QComboBox();
            combo->addItem(tr("-- Select Layer --"), "");
            QList<QgsMapLayer*> layers = QgsProject::instance()->mapLayers().values();
            for (QgsMapLayer *layer : layers) {
                if (layer->type() == Qgis::LayerType::Raster) {
                    combo->addItem(layer->name(), layer->id());
                }
            }
            fieldWidget = combo;
        } else if (dynamic_cast<const QgsProcessingParameterVectorLayer*>(param)) {
            // For vector layers, create a combo box with available layers
            QComboBox *combo = new QComboBox();
            combo->addItem(tr("-- Select Layer --"), "");
            QList<QgsMapLayer*> layers = QgsProject::instance()->mapLayers().values();
            for (QgsMapLayer *layer : layers) {
                if (layer->type() == Qgis::LayerType::Vector) {
                    combo->addItem(layer->name(), layer->id());
                }
            }
            fieldWidget = combo;
        } else if (dynamic_cast<const QgsProcessingParameterRasterDestination*>(param) ||
                   dynamic_cast<const QgsProcessingParameterFileDestination*>(param)) {
            // For output destinations, create a file browser
            QLineEdit *lineEdit = new QLineEdit();
            QPushButton *browseBtn = new QPushButton(tr("Browse..."));
            QHBoxLayout *fileLayout = new QHBoxLayout();
            fileLayout->addWidget(lineEdit);
            fileLayout->addWidget(browseBtn);
            QWidget *fileWidget = new QWidget();
            fileWidget->setLayout(fileLayout);
            fieldWidget = fileWidget;

            // Connect browse button
            connect(browseBtn, &QPushButton::clicked, this, [this, lineEdit, param]() {
                QString filePath = QFileDialog::getSaveFileName(this, tr("Select Output File"), "", tr("GeoTIFF (*.tif)"));
                if (!filePath.isEmpty()) {
                    lineEdit->setText(filePath);
                }
            });
        } else if (dynamic_cast<const QgsProcessingParameterString*>(param)) {
            fieldWidget = new QLineEdit();
        } else if (dynamic_cast<const QgsProcessingParameterNumber*>(param)) {
            const QgsProcessingParameterNumber *numParam = static_cast<const QgsProcessingParameterNumber*>(param);
            if (numParam->dataType() == Qgis::ProcessingNumberParameterType::Integer) {
                QSpinBox *spinBox = new QSpinBox();
                spinBox->setMinimum(static_cast<int>(numParam->minimum()));
                spinBox->setMaximum(static_cast<int>(numParam->maximum()));
                spinBox->setValue(numParam->defaultValue().toInt());
                fieldWidget = spinBox;
            } else {
                QDoubleSpinBox *spinBox = new QDoubleSpinBox();
                spinBox->setMinimum(numParam->minimum());
                spinBox->setMaximum(numParam->maximum());
                spinBox->setValue(numParam->defaultValue().toDouble());
                fieldWidget = spinBox;
            }
        } else if (dynamic_cast<const QgsProcessingParameterBoolean*>(param)) {
            fieldWidget = new QCheckBox();
        } else {
            // Default: create a line edit
            fieldWidget = new QLineEdit();
        }

        if (fieldWidget) {
            rowLayout->addWidget(fieldWidget);
            paramFieldMap[param->name()] = fieldWidget;
        }

        paramLayout->addLayout(rowLayout);
    }

    // Add a stretch at the end
    paramLayout->addStretch();

    // Set the parameter panel as the main widget
    dlg->setMainWidget(qobject_cast<QgsPanelWidget*>(paramPanel));

    // After dialog closes, refresh canvas if algorithm was successful
    connect(dlg, &QgsProcessingAlgorithmDialogBase::algorithmFinished,
            this, [this](bool successful, const QVariantMap &) {
        if (successful)
        {
            refreshCanvasLayers();
            m_mapCanvas->refresh();
        }
    });

    dlg->exec();
    dlg->deleteLater();
}

void QgisDesktopWindow::openBandMathDialog()
{
    // Get selected raster layer
    QList<QgsMapLayer*> layers = selectedLayers();
    QgsRasterLayer *rasterLayer = nullptr;

    for (QgsMapLayer *layer : layers) {
        if (layer->type() == Qgis::LayerType::Raster) {
            rasterLayer = qobject_cast<QgsRasterLayer*>(layer);
            break;
        }
    }

    if (!rasterLayer) {
        QMessageBox::information(this, tr("Band Math"),
                                 tr("Please select a raster layer first."));
        return;
    }

    BandMathDialog dialog(this);
    dialog.setRasterLayer(rasterLayer);
    dialog.exec();
}

void QgisDesktopWindow::openSpectralIndexDialog()
{
    // Get selected raster layer
    QList<QgsMapLayer*> layers = selectedLayers();
    QgsRasterLayer *rasterLayer = nullptr;

    for (QgsMapLayer *layer : layers) {
        if (layer->type() == Qgis::LayerType::Raster) {
            rasterLayer = qobject_cast<QgsRasterLayer*>(layer);
            break;
        }
    }

    if (!rasterLayer) {
        QMessageBox::information(this, tr("Spectral Index"),
                                 tr("Please select a raster layer first."));
        return;
    }

    SpectralIndexDialog dialog(this);
    dialog.setRasterLayer(rasterLayer);
    dialog.exec();
}

void QgisDesktopWindow::openAtmosphericCorrectionDialog()
{
    // Get selected raster layer
    QList<QgsMapLayer*> layers = selectedLayers();
    QgsRasterLayer *rasterLayer = nullptr;

    for (QgsMapLayer *layer : layers) {
        if (layer->type() == Qgis::LayerType::Raster) {
            rasterLayer = qobject_cast<QgsRasterLayer*>(layer);
            break;
        }
    }

    if (!rasterLayer) {
        QMessageBox::information(this, tr("Atmospheric Correction"),
                                 tr("Please select a raster layer first."));
        return;
    }

    AtmosphericDialog dialog(this);
    dialog.setRasterLayer(rasterLayer);
    dialog.exec();
}

void QgisDesktopWindow::openContrastStretchDialog()
{
    QList<QgsMapLayer*> layers = selectedLayers();
    QgsRasterLayer *rasterLayer = nullptr;
    for (QgsMapLayer *layer : layers) {
        if (layer->type() == Qgis::LayerType::Raster) {
            rasterLayer = qobject_cast<QgsRasterLayer*>(layer);
            break;
        }
    }
    if (!rasterLayer) {
        QMessageBox::information(this, tr("Contrast Stretch"),
                                 tr("Please select a raster layer first."));
        return;
    }
    ContrastStretchDialog dialog(this);
    dialog.setRasterLayer(rasterLayer);
    dialog.exec();
}

void QgisDesktopWindow::openSpatialFilterDialog()
{
    QList<QgsMapLayer*> layers = selectedLayers();
    QgsRasterLayer *rasterLayer = nullptr;
    for (QgsMapLayer *layer : layers) {
        if (layer->type() == Qgis::LayerType::Raster) {
            rasterLayer = qobject_cast<QgsRasterLayer*>(layer);
            break;
        }
    }
    if (!rasterLayer) {
        QMessageBox::information(this, tr("Spatial Filter"),
                                 tr("Please select a raster layer first."));
        return;
    }
    SpatialFilterDialog dialog(this);
    dialog.setRasterLayer(rasterLayer);
    dialog.exec();
}

void QgisDesktopWindow::openPcaDialog()
{
    QList<QgsMapLayer*> layers = selectedLayers();
    QgsRasterLayer *rasterLayer = nullptr;
    for (QgsMapLayer *layer : layers) {
        if (layer->type() == Qgis::LayerType::Raster) {
            rasterLayer = qobject_cast<QgsRasterLayer*>(layer);
            break;
        }
    }
    if (!rasterLayer) {
        QMessageBox::information(this, tr("PCA"),
                                 tr("Please select a raster layer first."));
        return;
    }
    PcaDialog dialog(this);
    dialog.setRasterLayer(rasterLayer);
    dialog.exec();
}

void QgisDesktopWindow::openBandRatioDialog()
{
    QList<QgsMapLayer*> layers = selectedLayers();
    QgsRasterLayer *rasterLayer = nullptr;
    for (QgsMapLayer *layer : layers) {
        if (layer->type() == Qgis::LayerType::Raster) {
            rasterLayer = qobject_cast<QgsRasterLayer*>(layer);
            break;
        }
    }
    if (!rasterLayer) {
        QMessageBox::information(this, tr("Band Ratio / IHS"),
                                 tr("Please select a raster layer first."));
        return;
    }
    BandRatioDialog dialog(this);
    dialog.setRasterLayer(rasterLayer);
    dialog.exec();
}

void QgisDesktopWindow::openCrsPresetDialog()
{
    CrsPresetDialog dlg( this );
    if ( dlg.exec() == QDialog::Accepted )
    {
        int epsg = dlg.selectedEpsg();
        if ( epsg > 0 )
        {
            QgsCoordinateReferenceSystem crs = QgsCoordinateReferenceSystem::fromEpsgId( epsg );
            if ( crs.isValid() )
            {
                QgsProject::instance()->setCrs( crs );
                m_mapCanvas->setDestinationCrs( crs );
                m_mapCanvas->refresh();
                updateCrsDisplay();
                statusBar()->showMessage( tr( "Project CRS set to: %1" ).arg( crs.authid() ), 3000 );
            }
        }
    }
}

void QgisDesktopWindow::setLayerCrsFromPreset()
{
    QList<QgsMapLayer *> selected = selectedLayers();
    if ( selected.isEmpty() )
    {
        QMessageBox::information( this, tr( "Set Layer CRS" ), tr( "No layer selected." ) );
        return;
    }

    QgsMapLayer *layer = selected.first();

    CrsPresetDialog dlg( this );
    if ( dlg.exec() == QDialog::Accepted )
    {
        int epsg = dlg.selectedEpsg();
        if ( epsg > 0 )
        {
            QgsCoordinateReferenceSystem crs = QgsCoordinateReferenceSystem::fromEpsgId( epsg );
            if ( crs.isValid() )
            {
                layer->setCrs( crs );
                m_mapCanvas->refresh();
                statusBar()->showMessage(
                    tr( "Layer CRS set to: %1 for \"%2\"" ).arg( crs.authid(), layer->name() ), 3000 );
            }
        }
    }
}

void QgisDesktopWindow::savePanelState()
{
    QSettings settings;
    settings.setValue( "mainwindow/state", saveState() );
    settings.setValue( "mainwindow/geometry", saveGeometry() );
}

void QgisDesktopWindow::restorePanelState()
{
    QSettings settings;
    QByteArray state = settings.value( "mainwindow/state" ).toByteArray();
    if ( !state.isEmpty() )
    {
        restoreState( state );
    }
    QByteArray geometry = settings.value( "mainwindow/geometry" ).toByteArray();
    if ( !geometry.isEmpty() )
    {
        restoreGeometry( geometry );
    }
}

void QgisDesktopWindow::resetPanelLayout()
{
    QSettings settings;
    settings.remove( "mainwindow/state" );
    settings.remove( "mainwindow/geometry" );

    // Reset to default layout
    if ( m_layersDock )
        addDockWidget( Qt::LeftDockWidgetArea, m_layersDock );
    if ( m_browserDock )
        addDockWidget( Qt::LeftDockWidgetArea, m_browserDock );
    if ( m_processingDock )
        addDockWidget( Qt::RightDockWidgetArea, m_processingDock );
    if ( m_overviewDock )
        addDockWidget( Qt::RightDockWidgetArea, m_overviewDock );
    if ( m_identifyDock )
        addDockWidget( Qt::RightDockWidgetArea, m_identifyDock );
    if ( m_spectralDock )
        addDockWidget( Qt::RightDockWidgetArea, m_spectralDock );

    statusBar()->showMessage( tr( "Layout reset to defaults" ), 3000 );
}

void QgisDesktopWindow::closeEvent( QCloseEvent *event )
{
    savePanelState();
    event->accept();
}
