/*
 * SICNU GEO RS - Professional QGIS Desktop Interface (Pure C++)
 *
 * Full QGIS-compatible interface with:
 * - Layer tree with right-click context menu
 * - Raster/Vector layer properties dialogs
 * - CRS/Projection selection
 * - Native QGIS rendering performance
 */

#include <QApplication>
#include <QMainWindow>
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
#include <QInputDialog>
#include <QTabWidget>
#include <QFormLayout>
#include <QGroupBox>
#include <QSlider>
#include <QComboBox>
#include <QTextEdit>
#include <QListWidget>
#include <QDialogButtonBox>
#include <QCheckBox>
#include <QPushButton>

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
#include "src/processing/sicnunativealgorithms.h"

// Python embedding
#include "src/python/qgis_python.h"
#include "src/gui/python_console_widget.h"

// Forward declare for MOC
class LayerTreeMenuProvider;
class QgisDesktopWindow;


class QgisDesktopWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit QgisDesktopWindow(QWidget *parent = nullptr)
        : QMainWindow(parent)
    {
        setWindowTitle("SICNU GEO RS - Remote Sensing Analysis");
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
        qDebug() << "Window initialized";
    }

    void setupUi()
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

    void setupMapCanvas()
    {
        m_mapCanvas = new QgsMapCanvas(m_mapCanvasContainer);

        QVBoxLayout *layout = new QVBoxLayout(m_mapCanvasContainer);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->addWidget(m_mapCanvas);

        // ── Performance Optimization (matching QGIS defaults) ─────────────────
        // Enable parallel rendering for multi-threaded map rendering
        m_mapCanvas->setParallelRenderingEnabled(true);

        // Set map update interval (ms) - controls how often canvas redraws during interactions
        // QGIS default is 250ms
        m_mapCanvas->setMapUpdateInterval(250);

        // Enable preview jobs for smoother zoom transitions
        m_mapCanvas->setPreviewJobsEnabled(true);

        // ── Visual Settings (matching QGIS defaults) ──────────────────────────
        // Canvas background color (QGIS default: white)
        m_mapCanvas->setCanvasColor(Qt::white);

        // Enable anti-aliasing for better rendering quality
        m_mapCanvas->enableAntiAliasing(true);

        // Selection color (QGIS default: yellow #ffff00 with 100 alpha)
        m_mapCanvas->setSelectionColor(QColor(255, 255, 0, 100));

        // ── Map Tools ─────────────────────────────────────────────────────────
        m_panTool = new QgsMapToolPan(m_mapCanvas);
        m_zoomInTool = new QgsMapToolZoom(m_mapCanvas, false);
        m_zoomOutTool = new QgsMapToolZoom(m_mapCanvas, true);
        m_identifyTool = new QgsMapToolIdentify(m_mapCanvas);

        // Set default tool (QGIS default: pan)
        m_mapCanvas->setMapTool(m_panTool);
    }

    void setupMenu()
    {
        // Project Menu
        QMenu *projectMenu = menuBar()->addMenu("&Project");
        projectMenu->addAction("New Project", this, &QgisDesktopWindow::newProject, QKeySequence::New);
        projectMenu->addAction("Open Project...", this, &QgisDesktopWindow::openProject, QKeySequence::Open);
        projectMenu->addAction("Save Project", this, &QgisDesktopWindow::saveProject, QKeySequence::Save);
        projectMenu->addAction("Save Project As...", this, &QgisDesktopWindow::saveProjectAs);
        projectMenu->addSeparator();
        projectMenu->addAction("Import Layer...", this, &QgisDesktopWindow::importLayer);
        projectMenu->addSeparator();
        projectMenu->addAction("Quit", this, &QMainWindow::close, QKeySequence::Quit);

        // Edit Menu
        QMenu *editMenu = menuBar()->addMenu("&Edit");
        editMenu->addAction("Undo", this, &QgisDesktopWindow::undo, QKeySequence::Undo);
        editMenu->addAction("Redo", this, &QgisDesktopWindow::redo, QKeySequence::Redo);
        editMenu->addSeparator();
        editMenu->addAction("Cut Features", this, &QgisDesktopWindow::cutFeatures, QKeySequence::Cut);
        editMenu->addAction("Copy Features", this, &QgisDesktopWindow::copyFeatures, QKeySequence::Copy);
        editMenu->addAction("Paste Features", this, &QgisDesktopWindow::pasteFeatures, QKeySequence::Paste);
        editMenu->addSeparator();
        editMenu->addAction("Select All", this, &QgisDesktopWindow::selectAll, QKeySequence("Ctrl+A"));

        // View Menu
        QMenu *viewMenu = menuBar()->addMenu("&View");
        viewMenu->addAction("Zoom In", this, &QgisDesktopWindow::zoomIn, QKeySequence::ZoomIn);
        viewMenu->addAction("Zoom Out", this, &QgisDesktopWindow::zoomOut, QKeySequence::ZoomOut);
        viewMenu->addAction("Zoom Full", this, &QgisDesktopWindow::zoomFullExtent, QKeySequence("Ctrl+Shift+F"));
        viewMenu->addAction("Zoom to Layer", this, &QgisDesktopWindow::zoomToLayer, QKeySequence("Ctrl+L"));
        viewMenu->addSeparator();
        viewMenu->addAction("Pan", this, &QgisDesktopWindow::panMap, QKeySequence("Space"));
        viewMenu->addAction("Identify", this, &QgisDesktopWindow::identifyFeatures, QKeySequence("Ctrl+Shift+I"));
        viewMenu->addSeparator();
        viewMenu->addAction("Refresh", this, &QgisDesktopWindow::refreshMap, QKeySequence("F5"));

        // Layer Menu
        QMenu *layerMenu = menuBar()->addMenu("&Layer");
        layerMenu->addAction("Add Raster Layer...", this, &QgisDesktopWindow::addRasterLayer);
        layerMenu->addAction("Add Vector Layer...", this, &QgisDesktopWindow::addVectorLayer);
        layerMenu->addSeparator();
        layerMenu->addAction("Layer Properties...", this, &QgisDesktopWindow::layerProperties, QKeySequence("Ctrl+I"));
        layerMenu->addAction("Remove Layer", this, &QgisDesktopWindow::removeLayer, QKeySequence::Delete);
        layerMenu->addSeparator();
        layerMenu->addAction("Set Project CRS...", this, &QgisDesktopWindow::setProjectCrs);

        // Settings Menu
        QMenu *settingsMenu = menuBar()->addMenu("&Settings");
        settingsMenu->addAction("Options...", this, &QgisDesktopWindow::options);

        // Processing Menu
        QMenu *processingMenu = menuBar()->addMenu("&Processing");
        processingMenu->addAction("Toolbox", this, &QgisDesktopWindow::showProcessingToolbox);
        processingMenu->addSeparator();
        processingMenu->addAction("History", this, &QgisDesktopWindow::showProcessingHistory);
        processingMenu->addAction("Python Console", this, [this]() {
            if (auto *dock = findChild<QgsDockWidget *>("pythonDock"))
                dock->setVisible(true);
        });

        // Help Menu
        QMenu *helpMenu = menuBar()->addMenu("&Help");
        helpMenu->addAction("Help Contents", this, &QgisDesktopWindow::helpContents, QKeySequence::HelpContents);
        helpMenu->addSeparator();
        helpMenu->addAction("Check Version", this, &QgisDesktopWindow::checkVersion);
        helpMenu->addAction("About", this, &QgisDesktopWindow::about);
    }

    void setupToolbars()
    {
        // File Toolbar
        QToolBar *fileToolBar = addToolBar("File");
        fileToolBar->setObjectName("fileToolBar");
        fileToolBar->addAction("New", this, &QgisDesktopWindow::newProject);
        fileToolBar->addAction("Open", this, &QgisDesktopWindow::openProject);
        fileToolBar->addAction("Save", this, &QgisDesktopWindow::saveProject);
        fileToolBar->addSeparator();
        fileToolBar->addAction("Add Raster", this, &QgisDesktopWindow::addRasterLayer);
        fileToolBar->addAction("Add Vector", this, &QgisDesktopWindow::addVectorLayer);

        // Map Tools Toolbar
        QToolBar *mapToolsToolBar = addToolBar("Map Tools");
        mapToolsToolBar->setObjectName("mapToolsToolBar");
        mapToolsToolBar->addAction("Pan", this, &QgisDesktopWindow::panMap);
        mapToolsToolBar->addAction("Zoom In", this, &QgisDesktopWindow::zoomIn);
        mapToolsToolBar->addAction("Zoom Out", this, &QgisDesktopWindow::zoomOut);
        mapToolsToolBar->addAction("Full Extent", this, &QgisDesktopWindow::zoomFullExtent);
        mapToolsToolBar->addSeparator();
        mapToolsToolBar->addAction("Identify", this, &QgisDesktopWindow::identifyFeatures);

        // CRS Selector in toolbar
        m_crsSelector = new QgsProjectionSelectionWidget(mapToolsToolBar);
        m_crsSelector->setOptionVisible(QgsProjectionSelectionWidget::ProjectCrs, true);
        connect(m_crsSelector, &QgsProjectionSelectionWidget::crsChanged,
                this, &QgisDesktopWindow::onCrsChanged);
        mapToolsToolBar->addSeparator();
        mapToolsToolBar->addWidget(m_crsSelector);
    }

    void setupDockWidgets()
    {
        // Layers Panel (Left)
        QgsDockWidget *layersDock = new QgsDockWidget("Layers", this);
        layersDock->setObjectName("layersDock");
        layersDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

        QWidget *layersContainer = new QWidget(layersDock);
        QVBoxLayout *layersLayout = new QVBoxLayout(layersContainer);
        layersLayout->setContentsMargins(0, 0, 0, 0);

        // Create QGIS C++ layer tree view
        m_layerTreeView = new QgsLayerTreeView(layersContainer);
        m_layerTreeView->setHeaderHidden(false);

        layersLayout->addWidget(m_layerTreeView);

        layersDock->setWidget(layersContainer);
        addDockWidget(Qt::LeftDockWidgetArea, layersDock);

        // Browser Panel (Left, below layers)
        QgsDockWidget *browserDock = new QgsDockWidget("Browser", this);
        browserDock->setObjectName("browserDock");
        browserDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
        QLabel *browserPlaceholder = new QLabel("Browser Panel\n\nComing Soon...", browserDock);
        browserPlaceholder->setAlignment(Qt::AlignCenter);
        browserPlaceholder->setStyleSheet("color: #666; font-style: italic;");
        browserDock->setWidget(browserPlaceholder);
        addDockWidget(Qt::LeftDockWidgetArea, browserDock);

        // Tabify the left dock widgets
        tabifyDockWidget(layersDock, browserDock);
        layersDock->raise();

        // Processing Toolbox Panel (Left, below browser)
        QgsDockWidget *processingDock = new QgsDockWidget("Processing Toolbox", this);
        processingDock->setObjectName("processingDock");
        processingDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

        m_toolboxView = new QgsProcessingToolboxTreeView(processingDock);
        m_toolboxView->setRegistry(QgsApplication::processingRegistry());
        processingDock->setWidget(m_toolboxView);
        addDockWidget(Qt::LeftDockWidgetArea, processingDock);

        // Tabify with browser
        tabifyDockWidget(browserDock, processingDock);

        // Python Console (Bottom)
        QgsDockWidget *pythonDock = new QgsDockWidget("Python Console", this);
        pythonDock->setObjectName("pythonDock");
        auto *pythonConsole = new PythonConsoleWidget(pythonDock);
        pythonDock->setWidget(pythonConsole);
        addDockWidget(Qt::BottomDockWidgetArea, pythonDock);

        // Double-click on algorithm in toolbox opens execution dialog
        connect(m_toolboxView, &QgsProcessingToolboxTreeView::doubleClicked, this, [this](const QModelIndex &index) {
            const QgsProcessingAlgorithm *alg = m_toolboxView->algorithmForIndex(index);
            if (!alg)
                return;

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
                    return QVariantMap();
                }

                QgsProcessingContext *processingContext() override
                {
                    return &mContext;
                }

            private:
                QgsProcessingContext mContext;
            };

            SimpleAlgorithmDialog *dlg = new SimpleAlgorithmDialog(this);
            dlg->setAlgorithm(algorithm);

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
        });

        // Overview Panel (Right)
        QgsDockWidget *overviewDock = new QgsDockWidget("Overview", this);
        overviewDock->setObjectName("overviewDock");
        overviewDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
        QLabel *overviewPlaceholder = new QLabel("Overview Map\n\nComing Soon...", overviewDock);
        overviewPlaceholder->setAlignment(Qt::AlignCenter);
        overviewPlaceholder->setStyleSheet("color: #666; font-style: italic;");
        overviewDock->setWidget(overviewPlaceholder);
        addDockWidget(Qt::RightDockWidgetArea, overviewDock);
    }

    void setupStatusBar()
    {
        QStatusBar *bar = statusBar();

        // CRS display
        m_crsLabel = new QLabel("EPSG:3857", bar);
        bar->addPermanentWidget(m_crsLabel);

        // Coordinates display
        m_coordinatesLabel = new QLabel("0.00, 0.00", bar);
        bar->addPermanentWidget(m_coordinatesLabel);

        // Scale display
        m_scaleLabel = new QLabel("Scale: 1:1000", bar);
        bar->addPermanentWidget(m_scaleLabel);

        // Render time display
        m_renderTimeLabel = new QLabel("", bar);
        bar->addPermanentWidget(m_renderTimeLabel);

        bar->showMessage("Ready", 3000);
    }

    void setupConnections()
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

        // Project signals
        connect(QgsProject::instance(), &QgsProject::readProject,
                this, &QgisDesktopWindow::onProjectRead);
        connect(QgsProject::instance(), &QgsProject::writeProject,
                this, &QgisDesktopWindow::onProjectWrite);
    }

    // Initialize layer tree view with project (matching QGIS behavior)
    void initLayerTree()
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

        // Connect layer tree to canvas for automatic updates
        connect(root, &QgsLayerTree::layerOrderChanged,
                this, &QgisDesktopWindow::refreshCanvasLayers);
        connect(project, &QgsProject::layerRemoved,
                this, &QgisDesktopWindow::refreshCanvasLayers);
        connect(project, &QgsProject::layerWasAdded,
                this, &QgisDesktopWindow::refreshCanvasLayers);
    }

private slots:
    // ── Project Actions ────────────────────────────────────────────────────────
    void newProject()
    {
        QgsProject::instance()->clear();
        m_mapCanvas->setLayers({});
        m_mapCanvas->refresh();
        statusBar()->showMessage("New project created", 3000);
    }

    void openProject()
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

    void saveProject()
    {
        if (QgsProject::instance()->fileName().isEmpty()) {
            saveProjectAs();
        } else {
            QgsProject::instance()->write();
            statusBar()->showMessage("Project saved", 3000);
        }
    }

    void saveProjectAs()
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

    void importLayer() { addRasterLayer(); }

    // ── Edit Actions ──────────────────────────────────────────────────────────
    void undo() { statusBar()->showMessage("Undo", 2000); }
    void redo() { statusBar()->showMessage("Redo", 2000); }
    void cutFeatures() { statusBar()->showMessage("Cut features", 2000); }
    void copyFeatures() { statusBar()->showMessage("Copy features", 2000); }
    void pasteFeatures() { statusBar()->showMessage("Paste features", 2000); }
    void selectAll() { statusBar()->showMessage("Select all", 2000); }

    // ── View Actions ──────────────────────────────────────────────────────────
    void zoomIn() { m_mapCanvas->zoomIn(); }
    void zoomOut() { m_mapCanvas->zoomOut(); }
    void panMap() { m_mapCanvas->setMapTool(m_panTool); }
    void identifyFeatures() { m_mapCanvas->setMapTool(m_identifyTool); }

    void zoomFullExtent()
    {
        m_mapCanvas->zoomToFullExtent();
        statusBar()->showMessage("Full extent", 2000);
    }

    void zoomToLayer()
    {
        QList<QgsMapLayer*> selected = selectedLayers();
        if (!selected.isEmpty()) {
            m_mapCanvas->setExtent(selected.first()->extent());
            m_mapCanvas->refresh();
            statusBar()->showMessage("Zoomed to layer", 2000);
        }
    }

    void refreshMap()
    {
        m_mapCanvas->refresh();
        statusBar()->showMessage("Map refreshed", 2000);
    }

public slots:
    // ── Layer Actions (public for LayerTreeMenuProvider) ──────────────────────
    void addRasterLayer()
    {
        QString filePath = QFileDialog::getOpenFileName(
            this, "Open Raster Layer",
            "/home/kevin/projects/exp-rs/data",
            "Raster Files (*.tif *.tiff *.img *.jp2 *.png *.jpg *.jpeg);;All Files (*.*)"
        );
        if (!filePath.isEmpty()) {
            loadRasterLayer(filePath);
        }
    }

    void addVectorLayer()
    {
        QString filePath = QFileDialog::getOpenFileName(
            this, "Open Vector Layer",
            "/home/kevin/projects/exp-rs/data",
            "Vector Files (*.shp *.gpkg *.geojson *.kml *.gml);;All Files (*.*)"
        );
        if (!filePath.isEmpty()) {
            loadVectorLayer(filePath);
        }
    }

private slots:
    void layerProperties()
    {
        QList<QgsMapLayer*> selected = selectedLayers();
        if (selected.isEmpty()) {
            QMessageBox::information(this, "Layer Properties", "No layer selected");
            return;
        }

        QgsMapLayer *layer = selected.first();
        showLayerProperties(layer);
    }

    void removeLayer()
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

    void setProjectCrs()
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
    void options()
    {
        QMessageBox::information(this, "Options",
            "SICNU GEO RS v1.0\n\n"
            "QGIS-based Remote Sensing Platform\n"
            "Built with QGIS C++ Libraries\n\n"
            "Settings dialog coming soon...");
    }

    // ── Processing Actions ─────────────────────────────────────────────────────
    void showProcessingToolbox()
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

    void showProcessingHistory()
    {
        QMessageBox::information(this, "Processing History", "Processing history coming soon...");
    }

    // ── Help Actions ──────────────────────────────────────────────────────────
    void helpContents() { QMessageBox::information(this, "Help", "QGIS Help"); }
    void checkVersion() { QMessageBox::information(this, "Version", "SICNU GEO RS v1.0"); }
    void about()
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
    void showCoordinates(const QgsPointXY &point)
    {
        m_coordinatesLabel->setText(QString("%1, %2")
            .arg(point.x(), 0, 'f', 2)
            .arg(point.y(), 0, 'f', 2));
    }

    void updateScale()
    {
        double s = m_mapCanvas->scale();
        m_scaleLabel->setText(QString("Scale: 1:%1").arg(static_cast<int>(s)));
    }

    void updateExtents()
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

    void updateCrsDisplay()
    {
        QgsCoordinateReferenceSystem crs = QgsProject::instance()->crs();
        m_crsLabel->setText(crs.authid());
        if (m_crsSelector) {
            m_crsSelector->setCrs(crs);
        }
    }

    void onRenderComplete(QPainter *painter)
    {
        Q_UNUSED(painter);
        // Render time is tracked internally by QGIS
    }

    void onProjectRead(const QDomDocument &doc)
    {
        Q_UNUSED(doc);
        refreshCanvasLayers();
        updateCrsDisplay();
        statusBar()->showMessage("Project loaded", 3000);
    }

    void onProjectWrite(QDomDocument &doc)
    {
        Q_UNUSED(doc);
        statusBar()->showMessage("Project saved", 2000);
    }

    void onCrsChanged(const QgsCoordinateReferenceSystem &crs)
    {
        QgsProject::instance()->setCrs(crs);
        m_mapCanvas->setDestinationCrs(crs);
        m_mapCanvas->refresh();
        updateCrsDisplay();
    }

    // ── Layer Tree Events ─────────────────────────────────────────────────────
    void onLayerTreeClicked(const QModelIndex &index)
    {
        Q_UNUSED(index);
        // Don't refresh on every click - only on selection change
    }

    void onLayerTreeDoubleClicked(const QModelIndex &index)
    {
        QgsLayerTreeNode *node = m_layerTreeModel->index2node(index);
        if (node && node->nodeType() == QgsLayerTreeNode::NodeLayer) {
            QgsLayerTreeLayer *layerNode = static_cast<QgsLayerTreeLayer*>(node);
            if (layerNode->layer()) {
                showLayerProperties(layerNode->layer());
            }
        }
    }

private:
    QgsLayerTreeGroup *findOrCreateGroup(const QString &name)
    {
        QgsLayerTree *root = QgsProject::instance()->layerTreeRoot();
        QgsLayerTreeGroup *group = root->findGroup(name);
        if (!group) {
            group = root->addGroup(name);
        }
        return group;
    }

    // ── Layer Loading ─────────────────────────────────────────────────────────
    void loadRasterLayer(const QString &filePath)
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

        QgsProject::instance()->addMapLayer(layer);

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

    void loadVectorLayer(const QString &filePath)
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

        QgsProject::instance()->addMapLayer(layer);

        QgsLayerTreeGroup *group = findOrCreateGroup("Vector Layers");
        group->addLayer(layer);

        // Zoom to layer extent first, then update layers (single refresh)
        m_mapCanvas->setExtent(layer->extent());
        refreshCanvasLayers();

        statusBar()->showMessage(QString("Loaded: %1 (%2 features)")
            .arg(name)
            .arg(layer->featureCount()), 3000);
    }

    void showLayerProperties(QgsMapLayer *layer)
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

    void refreshCanvasLayers()
    {
        QgsLayerTree *root = QgsProject::instance()->layerTreeRoot();
        QList<QgsMapLayer*> layers = root->layerOrder();
        m_mapCanvas->setLayers(layers);
    }

    QList<QgsMapLayer*> selectedLayers()
    {
        QList<QgsMapLayer*> result;
        QModelIndexList selected = m_layerTreeView->selectionModel()->selectedIndexes();
        for (const QModelIndex &idx : selected) {
            QgsLayerTreeNode *node = m_layerTreeModel->index2node(idx);
            if (node && node->nodeType() == QgsLayerTreeNode::NodeLayer) {
                QgsLayerTreeLayer *layerNode = static_cast<QgsLayerTreeLayer*>(node);
                if (layerNode->layer()) {
                    result.append(layerNode->layer());
                }
            }
        }
        return result;
    }

    // ── Member Variables ──────────────────────────────────────────────────────
    // QGIS C++ components
    QgsMapCanvas *m_mapCanvas = nullptr;
public:
    QgsMapCanvas *mapCanvas() const { return m_mapCanvas; }
    QgsLayerTreeView *m_layerTreeView = nullptr;
    QgsLayerTreeModel *m_layerTreeModel = nullptr;
    QWidget *m_mapCanvasContainer = nullptr;
    QgsProjectionSelectionWidget *m_crsSelector = nullptr;
    LayerTreeMenuProvider *m_layerTreeMenuProvider = nullptr;
    QgsProcessingToolboxTreeView *m_toolboxView = nullptr;

    // Map tools
    QgsMapToolPan *m_panTool = nullptr;
    QgsMapToolZoom *m_zoomInTool = nullptr;
    QgsMapToolZoom *m_zoomOutTool = nullptr;
    QgsMapToolIdentify *m_identifyTool = nullptr;

    // Status bar widgets
    QLabel *m_crsLabel = nullptr;
    QLabel *m_coordinatesLabel = nullptr;
    QLabel *m_scaleLabel = nullptr;
    QLabel *m_renderTimeLabel = nullptr;
};

class LayerTreeMenuProvider : public QgsLayerTreeViewMenuProvider
{
public:
    LayerTreeMenuProvider(QgsLayerTreeView *view, QgsMapCanvas *canvas, QgisDesktopWindow *window)
        : mView(view), mCanvas(canvas), mWindow(window) {}

    QMenu *createContextMenu() override
    {
        QMenu *menu = new QMenu();
        QModelIndex index = mView->currentIndex();
        QgsLayerTreeNode *node = index.isValid() ? mView->layerTreeModel()->index2node(index) : nullptr;

        if (!node) {
            menu->addAction(QObject::tr("Add Raster Layer..."), mWindow, &QgisDesktopWindow::addRasterLayer);
            menu->addAction(QObject::tr("Add Vector Layer..."), mWindow, &QgisDesktopWindow::addVectorLayer);
            menu->addSeparator();
            menu->addAction(mView->defaultActions()->actionAddGroup());
            return menu;
        }

        QgsLayerTreeViewDefaultActions *defActions = mView->defaultActions();

        if (node->nodeType() == QgsLayerTreeNode::NodeGroup) {
            menu->addAction(defActions->actionZoomToGroup(mCanvas));
            menu->addAction(defActions->actionRenameGroupOrLayer());
            menu->addAction(defActions->actionRemoveGroupOrLayer());
            menu->addSeparator();
            menu->addAction(defActions->actionAddGroup());
            menu->addAction(defActions->actionMutuallyExclusiveGroup());
        } else if (node->nodeType() == QgsLayerTreeNode::NodeLayer) {
            QgsLayerTreeLayer *layerNode = static_cast<QgsLayerTreeLayer *>(node);
            QgsMapLayer *layer = layerNode->layer();

            menu->addAction(defActions->actionZoomToLayers(mCanvas));

            if (layer && layer->type() == Qgis::LayerType::Raster) {
                QAction *zoomNative = menu->addAction(QObject::tr("Zoom to Native Resolution (1:1)"));
                QObject::connect(zoomNative, &QAction::triggered, [this, layer]() {
                    QgsRasterLayer *rl = qobject_cast<QgsRasterLayer *>(layer);
                    if (rl) {
                        double xRes = rl->rasterUnitsPerPixelX();
                        double yRes = rl->rasterUnitsPerPixelY();
                        QgsRectangle ext = rl->extent();
                        double cx = (ext.xMinimum() + ext.xMaximum()) / 2.0;
                        double cy = (ext.yMinimum() + ext.yMaximum()) / 2.0;
                        double w = mCanvas->width() * xRes;
                        double h = mCanvas->height() * yRes;
                        mCanvas->setExtent(QgsRectangle(cx - w/2, cy - h/2, cx + w/2, cy + h/2));
                        mCanvas->refresh();
                    }
                });
            }

            menu->addAction(defActions->actionRenameGroupOrLayer());
            menu->addAction(defActions->actionShowFeatureCount());
            menu->addAction(defActions->actionRemoveGroupOrLayer());
            menu->addSeparator();
            menu->addAction(defActions->actionMoveToTop());
            menu->addAction(defActions->actionMoveToBottom());
            menu->addAction(defActions->actionGroupSelected());
        }

        menu->addSeparator();
        menu->addAction(QObject::tr("Add Raster Layer..."), mWindow, &QgisDesktopWindow::addRasterLayer);
        menu->addAction(QObject::tr("Add Vector Layer..."), mWindow, &QgisDesktopWindow::addVectorLayer);

        return menu;
    }

private:
    QgsLayerTreeView *mView;
    QgsMapCanvas *mCanvas;
    QgisDesktopWindow *mWindow;
};


int main(int argc, char *argv[])
{
    qDebug() << "Starting SICNU GEO RS...";

    // Create QGIS application (inherits QApplication, handles all Qt + QGIS init)
    // Heap-allocated to avoid destructor crash during DSO cleanup
    QgsApplication *app = new QgsApplication(argc, argv, true);
    app->setApplicationName("SICNU GEO RS");
    app->setApplicationVersion("1.0");
    app->setOrganizationName("SICNU");

    // Set prefix path and initialize providers (GDAL, PROJ, etc.)
    qDebug() << "Setting prefix path...";
    QgsApplication::setPrefixPath("/home/kevin/projects/exp-rs", true);
    qDebug() << "Initializing QGIS...";
    QgsApplication::initQgis();
    qDebug() << "QGIS initialized";

    // Register processing algorithms
    QgsApplication::processingRegistry()->addProvider( new SicnuNativeAlgorithms() );

    // Initialize Python embedding
    QgisPython::instance().initialize();
    QgisPython::instance().loadBindings();

    // Initialize QgsGui singleton (required for QGIS dialogs)
    qDebug() << "Initializing QgsGui...";
    QgsGui::instance();
    qDebug() << "QgsGui initialized";

    // Create and initialize layer tree
    qDebug() << "Creating window...";
    QgisDesktopWindow window;
    qDebug() << "Initializing layer tree...";
    window.initLayerTree();

    // Set up native QGIS context menu for layer tree
    window.m_layerTreeMenuProvider = new LayerTreeMenuProvider(window.m_layerTreeView, window.mapCanvas(), &window);
    window.m_layerTreeView->setMenuProvider(window.m_layerTreeMenuProvider);

    qDebug() << "Showing window...";
    window.show();
    qDebug() << "Window shown";

    // Auto-load sample data if available
    QString samplePath = "/home/kevin/projects/exp-rs/data/sample_crops.tif";
    if (QFileInfo::exists(samplePath)) {
        QTimer::singleShot(500, [&window, samplePath]() {
            auto *layer = new QgsRasterLayer(samplePath, "sample_crops");
            if (layer->isValid()) {
                QgsProject::instance()->addMapLayer(layer);
                QgsLayerTree *root = QgsProject::instance()->layerTreeRoot();
                QgsLayerTreeGroup *group = root->findGroup("Raster Layers");
                if (!group) group = root->addGroup("Raster Layers");
                group->addLayer(layer);
                window.mapCanvas()->setExtent(layer->extent());
                window.mapCanvas()->setLayers(root->layerOrder());
                window.mapCanvas()->refresh();
            }
        });
    }

    int result = app->exec();

    // Finalize Python before exit
    QgisPython::instance().finalize();

    return result;
}

#include "main.moc"
