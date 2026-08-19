// src/gui/main_window.cpp — LEGACY (VPATCH-7): not compiled, see header note.
#include "main_window.h"

#include <QVBoxLayout>
#include <QMenuBar>
#include <QToolBar>
#include <QStatusBar>
#include <QDockWidget>
#include <QLabel>
#include <QFileDialog>
#include <QMessageBox>
#include <QInputDialog>
#include <QTimer>
#include <QFileInfo>

#include <qgsapplication.h>
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
#include <raster/qgsrasterlayerproperties.h>
#include <vector/qgsvectorlayerproperties.h>
#include <proj/qgsprojectionselectionwidget.h>
#include <qgsgui.h>
#include <processing/qgsprocessingregistry.h>
#include <processing/qgsprocessingtoolboxtreeview.h>

#include "core/plugin_host.h"
#include "app/python/sicnu_app_interface.h"

SicnuMainWindow::SicnuMainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle("SICNU GEO RS - Remote Sensing Analysis");
    resize(1600, 1000);
}

SicnuMainWindow::~SicnuMainWindow()
{
    delete m_pluginHost;
}

void SicnuMainWindow::initialize()
{
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
    qDebug() << "Initializing layer tree...";
    initLayerTree();
    qDebug() << "Loading plugins...";
    loadPlugins();
    qDebug() << "Window initialized";
}

void SicnuMainWindow::setupUi()
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

void SicnuMainWindow::setupMapCanvas()
{
    m_mapCanvas = new QgsMapCanvas(m_mapCanvasContainer);

    QVBoxLayout *layout = new QVBoxLayout(m_mapCanvasContainer);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_mapCanvas);

    // Performance settings (matching QGIS defaults)
    m_mapCanvas->setParallelRenderingEnabled(true);
    m_mapCanvas->setMapUpdateInterval(250);
    m_mapCanvas->setPreviewJobsEnabled(true);
    m_mapCanvas->setCanvasColor(Qt::white);
    m_mapCanvas->enableAntiAliasing(true);
    m_mapCanvas->setSelectionColor(QColor(255, 255, 0, 100));

    // Map tools
    m_panTool = new QgsMapToolPan(m_mapCanvas);
    m_zoomInTool = new QgsMapToolZoom(m_mapCanvas, false);
    m_zoomOutTool = new QgsMapToolZoom(m_mapCanvas, true);
    m_identifyTool = new QgsMapToolIdentify(m_mapCanvas);
    m_mapCanvas->setMapTool(m_panTool);
}

void SicnuMainWindow::setupMenu()
{
    // Project Menu
    QMenu *projectMenu = menuBar()->addMenu("&Project");
    projectMenu->addAction("New Project", this, &SicnuMainWindow::newProject, QKeySequence::New);
    projectMenu->addAction("Open Project...", this, &SicnuMainWindow::openProject, QKeySequence::Open);
    projectMenu->addAction("Save Project", this, &SicnuMainWindow::saveProject, QKeySequence::Save);
    projectMenu->addAction("Save Project As...", this, &SicnuMainWindow::saveProjectAs);
    projectMenu->addSeparator();
    projectMenu->addAction("Import Layer...", this, &SicnuMainWindow::addRasterLayer);
    projectMenu->addSeparator();
    projectMenu->addAction("Quit", this, &QMainWindow::close, QKeySequence::Quit);

    // Edit Menu
    QMenu *editMenu = menuBar()->addMenu("&Edit");
    editMenu->addAction("Undo", this, [](){}, QKeySequence::Undo);
    editMenu->addAction("Redo", this, [](){}, QKeySequence::Redo);
    editMenu->addSeparator();
    editMenu->addAction("Select All", this, [](){}, QKeySequence("Ctrl+A"));

    // View Menu
    QMenu *viewMenu = menuBar()->addMenu("&View");
    viewMenu->addAction("Zoom In", this, &SicnuMainWindow::zoomIn, QKeySequence::ZoomIn);
    viewMenu->addAction("Zoom Out", this, &SicnuMainWindow::zoomOut, QKeySequence::ZoomOut);
    viewMenu->addAction("Zoom Full", this, &SicnuMainWindow::zoomFullExtent, QKeySequence("Ctrl+Shift+F"));
    viewMenu->addAction("Zoom to Layer", this, &SicnuMainWindow::zoomToLayer, QKeySequence("Ctrl+L"));
    viewMenu->addSeparator();
    viewMenu->addAction("Refresh", this, &SicnuMainWindow::refreshMap, QKeySequence("F5"));

    // Layer Menu
    QMenu *layerMenu = menuBar()->addMenu("&Layer");
    layerMenu->addAction("Add Raster Layer...", this, &SicnuMainWindow::addRasterLayer);
    layerMenu->addAction("Add Vector Layer...", this, &SicnuMainWindow::addVectorLayer);
    layerMenu->addSeparator();
    layerMenu->addAction("Layer Properties...", this, &SicnuMainWindow::layerProperties, QKeySequence("Ctrl+I"));
    layerMenu->addAction("Remove Layer", this, &SicnuMainWindow::removeLayer, QKeySequence::Delete);
    layerMenu->addSeparator();
    layerMenu->addAction("Set Project CRS...", this, &SicnuMainWindow::setProjectCrs);

    // Settings Menu
    QMenu *settingsMenu = menuBar()->addMenu("&Settings");
    settingsMenu->addAction("Options...", this, [](){});

    // Help Menu
    QMenu *helpMenu = menuBar()->addMenu("&Help");
    helpMenu->addAction("Help Contents", this, &SicnuMainWindow::helpContents, QKeySequence::HelpContents);
    helpMenu->addSeparator();
    helpMenu->addAction("Check Version", this, &SicnuMainWindow::checkVersion);
    helpMenu->addAction("About", this, &SicnuMainWindow::about);
}

void SicnuMainWindow::setupToolbars()
{
    // File Toolbar
    QToolBar *fileToolBar = addToolBar("File");
    fileToolBar->setObjectName("fileToolBar");
    fileToolBar->addAction("New", this, &SicnuMainWindow::newProject);
    fileToolBar->addAction("Open", this, &SicnuMainWindow::openProject);
    fileToolBar->addAction("Save", this, &SicnuMainWindow::saveProject);
    fileToolBar->addSeparator();
    fileToolBar->addAction("Add Raster", this, &SicnuMainWindow::addRasterLayer);
    fileToolBar->addAction("Add Vector", this, &SicnuMainWindow::addVectorLayer);

    // Map Tools Toolbar
    QToolBar *mapToolsToolBar = addToolBar("Map Tools");
    mapToolsToolBar->setObjectName("mapToolsToolBar");
    mapToolsToolBar->addAction("Pan", this, &SicnuMainWindow::panMap);
    mapToolsToolBar->addAction("Zoom In", this, &SicnuMainWindow::zoomIn);
    mapToolsToolBar->addAction("Zoom Out", this, &SicnuMainWindow::zoomOut);
    mapToolsToolBar->addAction("Full Extent", this, &SicnuMainWindow::zoomFullExtent);
    mapToolsToolBar->addSeparator();

    // CRS Selector
    m_crsSelector = new QgsProjectionSelectionWidget(mapToolsToolBar);
    m_crsSelector->setOptionVisible(QgsProjectionSelectionWidget::ProjectCrs, true);
    connect(m_crsSelector, &QgsProjectionSelectionWidget::crsChanged,
            this, &SicnuMainWindow::onCrsChanged);
    mapToolsToolBar->addSeparator();
    mapToolsToolBar->addWidget(m_crsSelector);
}

void SicnuMainWindow::setupDockWidgets()
{
    // Overview Panel (Right)
    QDockWidget *overviewDock = new QDockWidget("Overview", this);
    overviewDock->setObjectName("overviewDock");
    overviewDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    QLabel *overviewPlaceholder = new QLabel("Overview Map\n\nComing Soon...", overviewDock);
    overviewPlaceholder->setAlignment(Qt::AlignCenter);
    overviewPlaceholder->setStyleSheet("color: #666; font-style: italic;");
    overviewDock->setWidget(overviewPlaceholder);
    addDockWidget(Qt::RightDockWidgetArea, overviewDock);
}

void SicnuMainWindow::setupStatusBar()
{
    QStatusBar *bar = statusBar();

    m_crsLabel = new QLabel("EPSG:3857", bar);
    bar->addPermanentWidget(m_crsLabel);

    m_coordinatesLabel = new QLabel("0.00, 0.00", bar);
    bar->addPermanentWidget(m_coordinatesLabel);

    m_scaleLabel = new QLabel("Scale: 1:1000", bar);
    bar->addPermanentWidget(m_scaleLabel);

    m_renderTimeLabel = new QLabel("", bar);
    bar->addPermanentWidget(m_renderTimeLabel);

    bar->showMessage("Ready", 3000);
}

void SicnuMainWindow::setupConnections()
{
    connect(m_mapCanvas, &QgsMapCanvas::xyCoordinates,
            this, &SicnuMainWindow::showCoordinates);
    connect(m_mapCanvas, &QgsMapCanvas::scaleChanged,
            this, &SicnuMainWindow::updateScale);
    connect(m_mapCanvas, &QgsMapCanvas::extentsChanged,
            this, &SicnuMainWindow::updateExtents);
    connect(m_mapCanvas, &QgsMapCanvas::renderComplete,
            this, &SicnuMainWindow::onRenderComplete);

    connect(QgsProject::instance(), &QgsProject::readProject,
            this, &SicnuMainWindow::onProjectRead);
    connect(QgsProject::instance(), &QgsProject::writeProject,
            this, &SicnuMainWindow::onProjectWrite);
}

void SicnuMainWindow::initLayerTree()
{
    QgsProject *project = QgsProject::instance();
    QgsLayerTree *root = project->layerTreeRoot();

    m_layerTreeModel = new QgsLayerTreeModel(root, this);
    m_layerTreeModel->setFlag(QgsLayerTreeModel::ShowLegend);
    m_layerTreeModel->setFlag(QgsLayerTreeModel::ShowLegendAsTree);
    m_layerTreeModel->setFlag(QgsLayerTreeModel::UseEmbeddedWidgets);
    m_layerTreeModel->setFlag(QgsLayerTreeModel::UseTextFormatting);
    m_layerTreeModel->setFlag(QgsLayerTreeModel::AllowNodeReorder);
    m_layerTreeModel->setFlag(QgsLayerTreeModel::AllowNodeRename);
    m_layerTreeModel->setFlag(QgsLayerTreeModel::AllowNodeChangeVisibility);
    m_layerTreeModel->setFlag(QgsLayerTreeModel::AllowLegendChangeState);
    m_layerTreeModel->setFlag(QgsLayerTreeModel::ActionHierarchical);

    connect(project, &QgsProject::crsChanged,
            this, &SicnuMainWindow::updateCrsDisplay);
    connect(root, &QgsLayerTree::layerOrderChanged,
            this, &SicnuMainWindow::refreshCanvasLayers);
    connect(project, &QgsProject::layerRemoved,
            this, &SicnuMainWindow::refreshCanvasLayers);
    connect(project, &QgsProject::layerWasAdded,
            this, &SicnuMainWindow::refreshCanvasLayers);
}

void SicnuMainWindow::loadPlugins()
{
    m_appInterface = new SicnuAppInterface(this, nullptr, nullptr, this);
    m_pluginHost = new PluginHost(PluginHost::DEFAULT_PYTHON_POOL_SIZE, this);
    m_pluginHost->setAppInterface(m_appInterface);

    connect(m_pluginHost, &PluginHost::pluginLoaded, this, [this](const QString &name) {
        statusBar()->showMessage(QString("Plugin loaded: %1").arg(name), 3000);
    });

    connect(m_pluginHost, &PluginHost::pluginError, this, [this](const QString &name, const QString &error) {
        qWarning() << "Plugin error:" << name << error;
    });

    QString pluginDir = QCoreApplication::applicationDirPath() + "/../plugins";
    m_pluginHost->loadPlugins(pluginDir);
}

void SicnuMainWindow::addPluginDockWidget(const QString &pluginName, QDockWidget *dock)
{
    dock->setParent(this);
    addDockWidget(Qt::LeftDockWidgetArea, dock);
    m_pluginDocks[pluginName] = dock;
}

void SicnuMainWindow::addPluginMenu(const QString &pluginName, QMenu *menu)
{
    menuBar()->addMenu(menu);
    m_pluginMenus[pluginName] = menu;
}

void SicnuMainWindow::addPluginToolbar(const QString &pluginName, QToolBar *toolbar)
{
    addToolBar(toolbar);
    m_pluginToolbars[pluginName] = toolbar;
}

void SicnuMainWindow::newProject()
{
    QgsProject::instance()->clear();
    m_mapCanvas->setLayers({});
    m_mapCanvas->refresh();
    statusBar()->showMessage("New project created", 3000);
}

void SicnuMainWindow::openProject()
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

void SicnuMainWindow::saveProject()
{
    if (QgsProject::instance()->fileName().isEmpty()) {
        saveProjectAs();
    } else {
        QgsProject::instance()->write();
        statusBar()->showMessage("Project saved", 3000);
    }
}

void SicnuMainWindow::saveProjectAs()
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

void SicnuMainWindow::zoomIn() { m_mapCanvas->zoomIn(); }
void SicnuMainWindow::zoomOut() { m_mapCanvas->zoomOut(); }
void SicnuMainWindow::panMap() { m_mapCanvas->setMapTool(m_panTool); }
void SicnuMainWindow::identifyFeatures() { m_mapCanvas->setMapTool(m_identifyTool); }

void SicnuMainWindow::zoomFullExtent()
{
    m_mapCanvas->zoomToFullExtent();
    statusBar()->showMessage("Full extent", 2000);
}

void SicnuMainWindow::zoomToLayer()
{
    QList<QgsMapLayer*> selected = selectedLayers();
    if (!selected.isEmpty()) {
        m_mapCanvas->setExtent(selected.first()->extent());
        m_mapCanvas->refresh();
        statusBar()->showMessage("Zoomed to layer", 2000);
    }
}

void SicnuMainWindow::refreshMap()
{
    m_mapCanvas->refresh();
    statusBar()->showMessage("Map refreshed", 2000);
}

void SicnuMainWindow::addRasterLayer()
{
    QString filePath = QFileDialog::getOpenFileName(
        this, "Open Raster Layer", "",
        "Raster Files (*.tif *.tiff *.img *.jp2 *.png *.jpg *.jpeg);;All Files (*.*)"
    );
    if (!filePath.isEmpty()) {
        loadRasterLayer(filePath);
    }
}

void SicnuMainWindow::addVectorLayer()
{
    QString filePath = QFileDialog::getOpenFileName(
        this, "Open Vector Layer", "",
        "Vector Files (*.shp *.gpkg *.geojson *.kml *.gml);;All Files (*.*)"
    );
    if (!filePath.isEmpty()) {
        loadVectorLayer(filePath);
    }
}

void SicnuMainWindow::layerProperties()
{
    QList<QgsMapLayer*> selected = selectedLayers();
    if (selected.isEmpty()) {
        QMessageBox::information(this, "Layer Properties", "No layer selected");
        return;
    }
    showLayerProperties(selected.first());
}

void SicnuMainWindow::removeLayer()
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

void SicnuMainWindow::setProjectCrs()
{
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

void SicnuMainWindow::helpContents() { QMessageBox::information(this, "Help", "QGIS Help"); }
void SicnuMainWindow::checkVersion() { QMessageBox::information(this, "Version", "SICNU GEO RS v2.0"); }
void SicnuMainWindow::about()
{
    QMessageBox::about(this, "About",
        "SICNU GEO RS\n\n"
        "Professional Remote Sensing Analysis Platform\n"
        "Built with QGIS C++ Libraries\n\n"
        "Version 2.0\n\n"
        "Features:\n"
        "- Plugin-based architecture\n"
        "- Raster and vector layer support\n"
        "- QGIS-compatible layer properties\n"
        "- CRS/Projection selection\n"
        "- Native QGIS rendering performance");
}

void SicnuMainWindow::showCoordinates(const QgsPointXY &point)
{
    m_coordinatesLabel->setText(QString("%1, %2")
        .arg(point.x(), 0, 'f', 2)
        .arg(point.y(), 0, 'f', 2));
}

void SicnuMainWindow::updateScale()
{
    double s = m_mapCanvas->scale();
    m_scaleLabel->setText(QString("Scale: 1:%1").arg(static_cast<int>(s)));
}

void SicnuMainWindow::updateExtents()
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

void SicnuMainWindow::updateCrsDisplay()
{
    QgsCoordinateReferenceSystem crs = QgsProject::instance()->crs();
    m_crsLabel->setText(crs.authid());
    if (m_crsSelector) {
        m_crsSelector->setCrs(crs);
    }
}

void SicnuMainWindow::onRenderComplete(QPainter *painter) { Q_UNUSED(painter); }

void SicnuMainWindow::onProjectRead(const QDomDocument &doc)
{
    Q_UNUSED(doc);
    refreshCanvasLayers();
    updateCrsDisplay();
    statusBar()->showMessage("Project loaded", 3000);
}

void SicnuMainWindow::onProjectWrite(QDomDocument &doc)
{
    Q_UNUSED(doc);
    statusBar()->showMessage("Project saved", 2000);
}

void SicnuMainWindow::onCrsChanged(const QgsCoordinateReferenceSystem &crs)
{
    QgsProject::instance()->setCrs(crs);
    m_mapCanvas->setDestinationCrs(crs);
    m_mapCanvas->refresh();
    updateCrsDisplay();
}

void SicnuMainWindow::onLayerTreeClicked(const QModelIndex &index) { Q_UNUSED(index); }

void SicnuMainWindow::onLayerTreeDoubleClicked(const QModelIndex &index)
{
    QgsLayerTreeNode *node = m_layerTreeModel->index2node(index);
    if (node && node->nodeType() == QgsLayerTreeNode::NodeLayer) {
        QgsLayerTreeLayer *layerNode = static_cast<QgsLayerTreeLayer*>(node);
        if (layerNode->layer()) {
            showLayerProperties(layerNode->layer());
        }
    }
}

QgsLayerTreeGroup *SicnuMainWindow::findOrCreateGroup(const QString &name)
{
    QgsLayerTree *root = QgsProject::instance()->layerTreeRoot();
    QgsLayerTreeGroup *group = root->findGroup(name);
    if (!group) {
        group = root->addGroup(name);
    }
    return group;
}

void SicnuMainWindow::loadRasterLayer(const QString &filePath)
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

    m_mapCanvas->setExtent(layer->extent());
    refreshCanvasLayers();

    statusBar()->showMessage(QString("Loaded: %1 (%2x%3, %4 bands)")
        .arg(name)
        .arg(layer->width())
        .arg(layer->height())
        .arg(layer->bandCount()), 3000);
}

void SicnuMainWindow::loadVectorLayer(const QString &filePath)
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

    m_mapCanvas->setExtent(layer->extent());
    refreshCanvasLayers();

    statusBar()->showMessage(QString("Loaded: %1 (%2 features)")
        .arg(name)
        .arg(layer->featureCount()), 3000);
}

void SicnuMainWindow::showLayerProperties(QgsMapLayer *layer)
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

void SicnuMainWindow::refreshCanvasLayers()
{
    QgsLayerTree *root = QgsProject::instance()->layerTreeRoot();
    QList<QgsMapLayer*> layers = root->layerOrder();
    m_mapCanvas->setLayers(layers);
}

QList<QgsMapLayer*> SicnuMainWindow::selectedLayers()
{
    QList<QgsMapLayer*> result;
    if (!m_layerTree || !m_layerTreeModel) return result;

    QModelIndexList selected = m_layerTree->selectionModel()->selectedIndexes();
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
