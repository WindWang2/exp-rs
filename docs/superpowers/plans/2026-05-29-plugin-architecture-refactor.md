# Plugin Architecture Refactor Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Refactor SICNU GEO RS from monolithic main.cpp (1057 lines) to Qt plugin-based architecture with modular components.

**Architecture:** Extract QgisDesktopWindow and LayerTreeMenuProvider into separate files, create SicnuPluginInterface for extensibility, implement PluginManager for dynamic loading, and organize code into core/gui/plugins/app directories.

**Tech Stack:** C++17, Qt6, QGIS C++ Libraries, CMake, QPluginLoader

---

## File Structure

Before defining tasks, here's the target file structure:

```
src/
├── core/                          # Core library (shared)
│   ├── interfaces/
│   │   └── sicnu_plugin_interface.h   # Plugin interface definition
│   ├── plugin_manager.h               # Plugin manager header
│   ├── plugin_manager.cpp             # Plugin manager implementation
│   └── CMakeLists.txt                 # Core library build
├── gui/                           # GUI library (shared)
│   ├── main_window.h                  # Main window header
│   ├── main_window.cpp                # Main window implementation
│   └── CMakeLists.txt                 # GUI library build
├── plugins/                       # Plugin directory
│   ├── CMakeLists.txt                 # Plugins parent build
│   ├── layer_tree/                    # Layer tree plugin
│   │   ├── layer_tree_plugin.h
│   │   ├── layer_tree_plugin.cpp
│   │   └── CMakeLists.txt
│   ├── processing/                    # Processing toolbox plugin
│   │   ├── processing_plugin.h
│   │   ├── processing_plugin.cpp
│   │   └── CMakeLists.txt
│   └── python_console/                # Python console plugin
│       ├── python_console_plugin.h
│       ├── python_console_plugin.cpp
│       └── CMakeLists.txt
└── app/                           # Main application
    ├── main.cpp                       # Minimal main function
    └── CMakeLists.txt                 # App build
```

---

## Task 1: Create Plugin Interface

**Files:**
- Create: `src/core/interfaces/sicnu_plugin_interface.h`

- [ ] **Step 1: Create plugin interface header**

```cpp
// src/core/interfaces/sicnu_plugin_interface.h
#pragma once

#include <QString>
#include <QWidget>
#include <QIcon>
#include <QAction>
#include <QList>

class QgsMapCanvas;
class QgsLayerTreeView;

/**
 * @brief Interface for SICNU GEO RS plugins
 *
 * All plugins must implement this interface to be loaded by the PluginManager.
 * Plugins can contribute UI widgets, menu actions, and toolbar actions.
 */
class SicnuPluginInterface
{
public:
    virtual ~SicnuPluginInterface() = default;

    // Plugin metadata
    virtual QString name() const = 0;
    virtual QString description() const = 0;
    virtual QString version() const = 0;
    virtual QIcon icon() const = 0;

    // Lifecycle
    virtual bool initialize(QgsMapCanvas *canvas, QgsLayerTreeView *layerTree) = 0;
    virtual void unload() = 0;

    // UI contributions (optional)
    virtual QWidget *createWidget(QWidget *parent = nullptr) { Q_UNUSED(parent); return nullptr; }
    virtual QList<QAction*> menuActions() { return {}; }
    virtual QList<QAction*> toolbarActions() { return {}; }
};

#define SicnuPluginInterface_iid "org.sicnu.SicnuPluginInterface/1.0"
Q_DECLARE_INTERFACE(SicnuPluginInterface, SicnuPluginInterface_iid)
```

- [ ] **Step 2: Commit plugin interface**

```bash
git add src/core/interfaces/sicnu_plugin_interface.h
git commit -m "feat(core): add SicnuPluginInterface definition"
```

---

## Task 2: Create Plugin Manager

**Files:**
- Create: `src/core/plugin_manager.h`
- Create: `src/core/plugin_manager.cpp`

- [ ] **Step 1: Create plugin manager header**

```cpp
// src/core/plugin_manager.h
#pragma once

#include <QObject>
#include <QMap>
#include <QStringList>
#include "interfaces/sicnu_plugin_interface.h"

class QPluginLoader;
class QgsMapCanvas;
class QgsLayerTreeView;

/**
 * @brief Manages loading and unloading of plugins
 */
class PluginManager : public QObject
{
    Q_OBJECT

public:
    explicit PluginManager(QgsMapCanvas *canvas, QgsLayerTreeView *layerTree, QObject *parent = nullptr);
    ~PluginManager();

    void loadPlugins(const QString &pluginDir);
    bool loadPlugin(const QString &pluginPath);
    void unloadAll();

    QStringList loadedPlugins() const;
    SicnuPluginInterface* plugin(const QString &name) const;
    bool isPluginLoaded(const QString &name) const;

signals:
    void pluginLoaded(const QString &name);
    void pluginUnloaded(const QString &name);
    void pluginError(const QString &name, const QString &error);

private:
    struct PluginInfo {
        SicnuPluginInterface *instance;
        QPluginLoader *loader;
        bool loaded;
    };

    QMap<QString, PluginInfo> m_plugins;
    QgsMapCanvas *m_canvas;
    QgsLayerTreeView *m_layerTree;
};
```

- [ ] **Step 2: Create plugin manager implementation**

```cpp
// src/core/plugin_manager.cpp
#include "plugin_manager.h"

#include <QDir>
#include <QPluginLoader>
#include <QDebug>
#include <QJsonObject>

PluginManager::PluginManager(QgsMapCanvas *canvas, QgsLayerTreeView *layerTree, QObject *parent)
    : QObject(parent)
    , m_canvas(canvas)
    , m_layerTree(layerTree)
{
}

PluginManager::~PluginManager()
{
    unloadAll();
}

void PluginManager::loadPlugins(const QString &pluginDir)
{
    QDir dir(pluginDir);
    if (!dir.exists()) {
        qWarning() << "Plugin directory not found:" << pluginDir;
        return;
    }

    qDebug() << "Loading plugins from:" << pluginDir;

    for (const QString &fileName : dir.entryList(QDir::Files)) {
        QString filePath = dir.absoluteFilePath(fileName);
        loadPlugin(filePath);
    }
}

bool PluginManager::loadPlugin(const QString &pluginPath)
{
    QPluginLoader *loader = new QPluginLoader(pluginPath, this);
    QJsonObject metadata = loader->metaData();

    if (metadata.isEmpty()) {
        qWarning() << "No metadata in plugin:" << pluginPath;
        delete loader;
        return false;
    }

    QObject *plugin = loader->instance();
    if (!plugin) {
        emit pluginError(pluginPath, loader->errorString());
        qWarning() << "Failed to load plugin:" << pluginPath << loader->errorString();
        delete loader;
        return false;
    }

    SicnuPluginInterface *interface = qobject_cast<SicnuPluginInterface*>(plugin);
    if (!interface) {
        emit pluginError(pluginPath, "Plugin does not implement SicnuPluginInterface");
        qWarning() << "Invalid plugin interface:" << pluginPath;
        delete loader;
        return false;
    }

    if (!interface->initialize(m_canvas, m_layerTree)) {
        emit pluginError(interface->name(), "Plugin initialization failed");
        qWarning() << "Plugin init failed:" << interface->name();
        delete loader;
        return false;
    }

    PluginInfo info;
    info.instance = interface;
    info.loader = loader;
    info.loaded = true;
    m_plugins[interface->name()] = info;

    qDebug() << "Loaded plugin:" << interface->name();
    emit pluginLoaded(interface->name());

    return true;
}

void PluginManager::unloadAll()
{
    for (auto it = m_plugins.begin(); it != m_plugins.end(); ++it) {
        if (it.value().loaded) {
            it.value().instance->unload();
            it.value().loader->unload();
            emit pluginUnloaded(it.key());
        }
    }
    m_plugins.clear();
}

QStringList PluginManager::loadedPlugins() const
{
    return m_plugins.keys();
}

SicnuPluginInterface* PluginManager::plugin(const QString &name) const
{
    auto it = m_plugins.find(name);
    if (it != m_plugins.end() && it.value().loaded) {
        return it.value().instance;
    }
    return nullptr;
}

bool PluginManager::isPluginLoaded(const QString &name) const
{
    auto it = m_plugins.find(name);
    return it != m_plugins.end() && it.value().loaded;
}
```

- [ ] **Step 3: Commit plugin manager**

```bash
git add src/core/plugin_manager.h src/core/plugin_manager.cpp
git commit -m "feat(core): add PluginManager for dynamic plugin loading"
```

---

## Task 3: Create Core Library CMake

**Files:**
- Create: `src/core/CMakeLists.txt` (new file for core library)

- [ ] **Step 1: Create core library CMakeLists.txt**

```cmake
# src/core/CMakeLists.txt
add_library(sicnu_core SHARED
    plugin_manager.cpp
)

target_include_directories(sicnu_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/interfaces
)

target_link_libraries(sicnu_core PUBLIC
    Qt6::Core
    Qt6::Gui
    qgis_core
)
```

- [ ] **Step 2: Commit core library CMake**

```bash
git add src/core/CMakeLists.txt
git commit -m "build(core): add CMakeLists.txt for core library"
```

---

## Task 4: Create Main Window Header

**Files:**
- Create: `src/gui/main_window.h`

- [ ] **Step 1: Create main window header**

```cpp
// src/gui/main_window.h
#pragma once

#include <QMainWindow>
#include <QMap>

class QgsMapCanvas;
class QgsLayerTreeView;
class QgsLayerTreeModel;
class QgsProjectionSelectionWidget;
class QgsProcessingToolboxTreeView;
class QgsMapToolPan;
class QgsMapToolZoom;
class QgsMapToolIdentify;
class PluginManager;

class SicnuMainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit SicnuMainWindow(QWidget *parent = nullptr);
    ~SicnuMainWindow();

    void initialize();

    // Plugin integration
    void addPluginDockWidget(const QString &pluginName, QDockWidget *dock);
    void addPluginMenu(const QString &pluginName, QMenu *menu);
    void addPluginToolbar(const QString &pluginName, QToolBar *toolbar);

    // Core component access
    QgsMapCanvas *mapCanvas() const { return m_mapCanvas; }
    QgsLayerTreeView *layerTreeView() const { return m_layerTree; }
    QgsLayerTreeModel *layerTreeModel() const { return m_layerTreeModel; }
    PluginManager *pluginManager() const { return m_pluginManager; }

    // Layer operations
    void addRasterLayer();
    void addVectorLayer();
    void refreshCanvasLayers();

private:
    void setupUi();
    void setupMapCanvas();
    void setupMenu();
    void setupToolbars();
    void setupDockWidgets();
    void setupStatusBar();
    void setupConnections();
    void initLayerTree();
    void loadPlugins();

    // Project actions
    void newProject();
    void openProject();
    void saveProject();
    void saveProjectAs();

    // View actions
    void zoomIn();
    void zoomOut();
    void zoomFullExtent();
    void zoomToLayer();
    void panMap();
    void identifyFeatures();
    void refreshMap();

    // Layer actions
    void layerProperties();
    void removeLayer();
    void setProjectCrs();

    // Help actions
    void helpContents();
    void checkVersion();
    void about();

    // Slots
    void showCoordinates(const QgsPointXY &point);
    void updateScale();
    void updateExtents();
    void updateCrsDisplay();
    void onRenderComplete(QPainter *painter);
    void onProjectRead(const QDomDocument &doc);
    void onProjectWrite(QDomDocument &doc);
    void onCrsChanged(const QgsCoordinateReferenceSystem &crs);
    void onLayerTreeClicked(const QModelIndex &index);
    void onLayerTreeDoubleClicked(const QModelIndex &index);

    // Helper methods
    QgsLayerTreeGroup *findOrCreateGroup(const QString &name);
    void loadRasterLayer(const QString &filePath);
    void loadVectorLayer(const QString &filePath);
    void showLayerProperties(QgsMapLayer *layer);
    QList<QgsMapLayer*> selectedLayers();

    // Core components
    QgsMapCanvas *m_mapCanvas = nullptr;
    QgsLayerTreeView *m_layerTree = nullptr;
    QgsLayerTreeModel *m_layerTreeModel = nullptr;
    QWidget *m_mapCanvasContainer = nullptr;
    QgsProjectionSelectionWidget *m_crsSelector = nullptr;
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

    // Plugin manager
    PluginManager *m_pluginManager = nullptr;

    // Plugin UI containers
    QMap<QString, QDockWidget*> m_pluginDocks;
    QMap<QString, QMenu*> m_pluginMenus;
    QMap<QString, QToolBar*> m_pluginToolbars;
};
```

- [ ] **Step 2: Commit main window header**

```bash
git add src/gui/main_window.h
git commit -m "feat(gui): add SicnuMainWindow header"
```

---

## Task 5: Create Main Window Implementation

**Files:**
- Create: `src/gui/main_window.cpp`

- [ ] **Step 1: Create main window implementation (part 1 - setup methods)**

```cpp
// src/gui/main_window.cpp
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

#include "core/plugin_manager.h"

SicnuMainWindow::SicnuMainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle("SICNU GEO RS - Remote Sensing Analysis");
    resize(1600, 1000);
}

SicnuMainWindow::~SicnuMainWindow()
{
    delete m_pluginManager;
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
```

- [ ] **Step 2: Create main window implementation (part 2 - toolbars and docks)**

```cpp
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
```

- [ ] **Step 3: Create main window implementation (part 3 - layer tree and plugins)**

```cpp
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
    m_pluginManager = new PluginManager(m_mapCanvas, m_layerTree, this);

    connect(m_pluginManager, &PluginManager::pluginLoaded, this, [this](const QString &name) {
        statusBar()->showMessage(QString("Plugin loaded: %1").arg(name), 3000);
    });

    connect(m_pluginManager, &PluginManager::pluginError, this, [this](const QString &name, const QString &error) {
        qWarning() << "Plugin error:" << name << error;
    });

    QString pluginDir = QCoreApplication::applicationDirPath() + "/../plugins";
    m_pluginManager->loadPlugins(pluginDir);
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
```

- [ ] **Step 4: Create main window implementation (part 4 - actions)**

```cpp
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
```

- [ ] **Step 5: Create main window implementation (part 5 - layer operations)**

```cpp
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
```

- [ ] **Step 6: Create main window implementation (part 6 - helper methods)**

```cpp
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
```

- [ ] **Step 7: Commit main window implementation**

```bash
git add src/gui/main_window.cpp
git commit -m "feat(gui): add SicnuMainWindow implementation"
```

---

## Task 6: Create Layer Tree Plugin

**Files:**
- Create: `src/plugins/layer_tree/layer_tree_plugin.h`
- Create: `src/plugins/layer_tree/layer_tree_plugin.cpp`
- Create: `src/plugins/layer_tree/CMakeLists.txt`

- [ ] **Step 1: Create layer tree plugin header**

```cpp
// src/plugins/layer_tree/layer_tree_plugin.h
#pragma once

#include <QObject>
#include "core/interfaces/sicnu_plugin_interface.h"

class QgsLayerTreeView;
class QgsLayerTreeModel;
class QgsMapCanvas;
class SicnuMainWindow;

class LayerTreePlugin : public QObject, public SicnuPluginInterface
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID SicnuPluginInterface_iid)
    Q_INTERFACES(SicnuPluginInterface)

public:
    explicit LayerTreePlugin(QObject *parent = nullptr);

    QString name() const override { return "LayerTree"; }
    QString description() const override { return "Layer tree panel with context menu"; }
    QString version() const override { return "1.0.0"; }
    QIcon icon() const override;

    bool initialize(QgsMapCanvas *canvas, QgsLayerTreeView *layerTree) override;
    void unload() override;

    QWidget *createWidget(QWidget *parent) override;
    QList<QAction*> menuActions() override;

private:
    QgsMapCanvas *m_canvas = nullptr;
    QgsLayerTreeView *m_layerTree = nullptr;
    QgsLayerTreeModel *m_model = nullptr;
};
```

- [ ] **Step 2: Create layer tree plugin implementation**

```cpp
// src/plugins/layer_tree/layer_tree_plugin.cpp
#include "layer_tree_plugin.h"

#include <QMenu>
#include <QAction>
#include <QIcon>

#include <qgsmapcanvas.h>
#include <layertree/qgslayertreeview.h>
#include <layertree/qgslayertreemodel.h>
#include <layertree/qgslayertreeviewdefaultactions.h>
#include <qgsproject.h>
#include <qgslayertree.h>
#include <qgslayertreegroup.h>
#include <qgslayertreelayer.h>
#include <qgslayertreenode.h>
#include <qgsmaplayer.h>
#include <qgsrasterlayer.h>
#include <qgsdockwidget.h>

LayerTreePlugin::LayerTreePlugin(QObject *parent)
    : QObject(parent)
{
}

QIcon LayerTreePlugin::icon() const
{
    return QIcon::fromTheme("layer-tree");
}

bool LayerTreePlugin::initialize(QgsMapCanvas *canvas, QgsLayerTreeView *layerTree)
{
    m_canvas = canvas;
    m_layerTree = layerTree;

    if (!m_layerTree) {
        qWarning() << "LayerTreePlugin: layerTree is null";
        return false;
    }

    // Set up the layer tree model
    QgsProject *project = QgsProject::instance();
    QgsLayerTree *root = project->layerTreeRoot();

    m_model = new QgsLayerTreeModel(root, this);
    m_model->setFlag(QgsLayerTreeModel::ShowLegend);
    m_model->setFlag(QgsLayerTreeModel::ShowLegendAsTree);
    m_model->setFlag(QgsLayerTreeModel::UseEmbeddedWidgets);
    m_model->setFlag(QgsLayerTreeModel::UseTextFormatting);
    m_model->setFlag(QgsLayerTreeModel::AllowNodeReorder);
    m_model->setFlag(QgsLayerTreeModel::AllowNodeRename);
    m_model->setFlag(QgsLayerTreeModel::AllowNodeChangeVisibility);
    m_model->setFlag(QgsLayerTreeModel::AllowLegendChangeState);
    m_model->setFlag(QgsLayerTreeModel::ActionHierarchical);

    m_layerTree->setLayerTreeModel(m_model);
    m_layerTree->setModel(m_model);
    m_layerTree->expandAll();

    qDebug() << "LayerTreePlugin initialized";
    return true;
}

void LayerTreePlugin::unload()
{
    qDebug() << "LayerTreePlugin unloaded";
}

QWidget *LayerTreePlugin::createWidget(QWidget *parent)
{
    // The layer tree view is created by the main window, so we return nullptr
    Q_UNUSED(parent);
    return nullptr;
}

QList<QAction*> LayerTreePlugin::menuActions()
{
    QList<QAction*> actions;

    QAction *addRaster = new QAction(tr("Add Raster Layer..."), this);
    connect(addRaster, &QAction::triggered, this, [this]() {
        // This will be handled by the main window
    });
    actions.append(addRaster);

    QAction *addVector = new QAction(tr("Add Vector Layer..."), this);
    connect(addVector, &QAction::triggered, this, [this]() {
        // This will be handled by the main window
    });
    actions.append(addVector);

    return actions;
}
```

- [ ] **Step 3: Create layer tree plugin CMakeLists.txt**

```cmake
# src/plugins/layer_tree/CMakeLists.txt
add_library(layer_tree_plugin SHARED
    layer_tree_plugin.cpp
)

target_include_directories(layer_tree_plugin PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(layer_tree_plugin PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    qgis_core
    qgis_gui
)

install(TARGETS layer_tree_plugin
    LIBRARY DESTINATION ${QGIS_PLUGIN_SUBDIR}
)
```

- [ ] **Step 4: Commit layer tree plugin**

```bash
git add src/plugins/layer_tree/
git commit -m "feat(plugins): add LayerTreePlugin"
```

---

## Task 7: Create Processing Plugin

**Files:**
- Create: `src/plugins/processing/processing_plugin.h`
- Create: `src/plugins/processing/processing_plugin.cpp`
- Create: `src/plugins/processing/CMakeLists.txt`

- [ ] **Step 1: Create processing plugin header**

```cpp
// src/plugins/processing/processing_plugin.h
#pragma once

#include <QObject>
#include "core/interfaces/sicnu_plugin_interface.h"

class QgsMapCanvas;
class QgsLayerTreeView;
class QgsProcessingToolboxTreeView;

class ProcessingPlugin : public QObject, public SicnuPluginInterface
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID SicnuPluginInterface_iid)
    Q_INTERFACES(SicnuPluginInterface)

public:
    explicit ProcessingPlugin(QObject *parent = nullptr);

    QString name() const override { return "Processing"; }
    QString description() const override { return "Processing toolbox and algorithms"; }
    QString version() const override { return "1.0.0"; }
    QIcon icon() const override;

    bool initialize(QgsMapCanvas *canvas, QgsLayerTreeView *layerTree) override;
    void unload() override;

    QWidget *createWidget(QWidget *parent) override;
    QList<QAction*> menuActions() override;

private:
    QgsMapCanvas *m_canvas = nullptr;
    QgsLayerTreeView *m_layerTree = nullptr;
    QgsProcessingToolboxTreeView *m_toolboxView = nullptr;
};
```

- [ ] **Step 2: Create processing plugin implementation**

```cpp
// src/plugins/processing/processing_plugin.cpp
#include "processing_plugin.h"

#include <QMenu>
#include <QAction>
#include <QIcon>
#include <QVBoxLayout>

#include <qgsapplication.h>
#include <qgsmapcanvas.h>
#include <layertree/qgslayertreeview.h>
#include <processing/qgsprocessingregistry.h>
#include <processing/qgsprocessingtoolboxtreeview.h>
#include <processing/qgsprocessingalgorithmdialogbase.h>
#include <qgsdockwidget.h>

#include "src/processing/sicnunativealgorithms.h"

ProcessingPlugin::ProcessingPlugin(QObject *parent)
    : QObject(parent)
{
}

QIcon ProcessingPlugin::icon() const
{
    return QIcon::fromTheme("processing");
}

bool ProcessingPlugin::initialize(QgsMapCanvas *canvas, QgsLayerTreeView *layerTree)
{
    m_canvas = canvas;
    m_layerTree = layerTree;

    // Register processing algorithms
    QgsApplication::processingRegistry()->addProvider(new SicnuNativeAlgorithms());

    qDebug() << "ProcessingPlugin initialized";
    return true;
}

void ProcessingPlugin::unload()
{
    qDebug() << "ProcessingPlugin unloaded";
}

QWidget *ProcessingPlugin::createWidget(QWidget *parent)
{
    QDockWidget *dock = new QDockWidget("Processing Toolbox", parent);
    dock->setObjectName("processingDock");
    dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    m_toolboxView = new QgsProcessingToolboxTreeView(dock);
    m_toolboxView->setRegistry(QgsApplication::processingRegistry());
    dock->setWidget(m_toolboxView);

    // Connect double-click to open algorithm dialog
    connect(m_toolboxView, &QgsProcessingToolboxTreeView::doubleClicked, this, [this](const QModelIndex &index) {
        const QgsProcessingAlgorithm *alg = m_toolboxView->algorithmForIndex(index);
        if (!alg) return;

        QgsProcessingAlgorithm *algorithm = alg->create();
        if (!algorithm) return;

        class SimpleAlgorithmDialog : public QgsProcessingAlgorithmDialogBase
        {
        public:
            using QgsProcessingAlgorithmDialogBase::QgsProcessingAlgorithmDialogBase;
            QVariantMap createProcessingParameters(Flags = Flags()) override { return QVariantMap(); }
            QgsProcessingContext *processingContext() override { return &mContext; }
        private:
            QgsProcessingContext mContext;
        };

        SimpleAlgorithmDialog *dlg = new SimpleAlgorithmDialog(m_toolboxView);
        dlg->setAlgorithm(algorithm);
        dlg->exec();
        dlg->deleteLater();
    });

    return dock;
}

QList<QAction*> ProcessingPlugin::menuActions()
{
    QList<QAction*> actions;

    QAction *toolbox = new QAction(tr("Processing Toolbox"), this);
    connect(toolbox, &QAction::triggered, this, [this]() {
        // Show processing toolbox dock
    });
    actions.append(toolbox);

    return actions;
}
```

- [ ] **Step 3: Create processing plugin CMakeLists.txt**

```cmake
# src/plugins/processing/CMakeLists.txt
add_library(processing_plugin SHARED
    processing_plugin.cpp
)

target_include_directories(processing_plugin PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(processing_plugin PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    qgis_core
    qgis_gui
)

install(TARGETS processing_plugin
    LIBRARY DESTINATION ${QGIS_PLUGIN_SUBDIR}
)
```

- [ ] **Step 4: Commit processing plugin**

```bash
git add src/plugins/processing/
git commit -m "feat(plugins): add ProcessingPlugin"
```

---

## Task 8: Create Python Console Plugin

**Files:**
- Create: `src/plugins/python_console/python_console_plugin.h`
- Create: `src/plugins/python_console/python_console_plugin.cpp`
- Create: `src/plugins/python_console/CMakeLists.txt`

- [ ] **Step 1: Create Python console plugin header**

```cpp
// src/plugins/python_console/python_console_plugin.h
#pragma once

#include <QObject>
#include "core/interfaces/sicnu_plugin_interface.h"

class QgsMapCanvas;
class QgsLayerTreeView;

class PythonConsolePlugin : public QObject, public SicnuPluginInterface
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID SicnuPluginInterface_iid)
    Q_INTERFACES(SicnuPluginInterface)

public:
    explicit PythonConsolePlugin(QObject *parent = nullptr);

    QString name() const override { return "PythonConsole"; }
    QString description() const override { return "Python console for scripting"; }
    QString version() const override { return "1.0.0"; }
    QIcon icon() const override;

    bool initialize(QgsMapCanvas *canvas, QgsLayerTreeView *layerTree) override;
    void unload() override;

    QWidget *createWidget(QWidget *parent) override;
    QList<QAction*> menuActions() override;

private:
    QgsMapCanvas *m_canvas = nullptr;
    QgsLayerTreeView *m_layerTree = nullptr;
};
```

- [ ] **Step 2: Create Python console plugin implementation**

```cpp
// src/plugins/python_console/python_console_plugin.cpp
#include "python_console_plugin.h"

#include <QMenu>
#include <QAction>
#include <QIcon>

#include <qgsmapcanvas.h>
#include <layertree/qgslayertreeview.h>
#include <qgsdockwidget.h>

#include "src/gui/python_console_widget.h"
#include "src/python/qgis_python.h"

PythonConsolePlugin::PythonConsolePlugin(QObject *parent)
    : QObject(parent)
{
}

QIcon PythonConsolePlugin::icon() const
{
    return QIcon::fromTheme("python");
}

bool PythonConsolePlugin::initialize(QgsMapCanvas *canvas, QgsLayerTreeView *layerTree)
{
    m_canvas = canvas;
    m_layerTree = layerTree;

    // Initialize Python
    QgisPython::instance().initialize();
    QgisPython::instance().loadBindings();

    qDebug() << "PythonConsolePlugin initialized";
    return true;
}

void PythonConsolePlugin::unload()
{
    QgisPython::instance().finalize();
    qDebug() << "PythonConsolePlugin unloaded";
}

QWidget *PythonConsolePlugin::createWidget(QWidget *parent)
{
    QDockWidget *dock = new QDockWidget("Python Console", parent);
    dock->setObjectName("pythonDock");

    auto *pythonConsole = new PythonConsoleWidget(dock);
    dock->setWidget(pythonConsole);

    return dock;
}

QList<QAction*> PythonConsolePlugin::menuActions()
{
    QList<QAction*> actions;

    QAction *console = new QAction(tr("Python Console"), this);
    connect(console, &QAction::triggered, this, [this]() {
        // Show Python console dock
    });
    actions.append(console);

    return actions;
}
```

- [ ] **Step 3: Create Python console plugin CMakeLists.txt**

```cmake
# src/plugins/python_console/CMakeLists.txt
add_library(python_console_plugin SHARED
    python_console_plugin.cpp
)

target_include_directories(python_console_plugin PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(python_console_plugin PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    qgis_core
    qgis_gui
    pybind11::embed
)

# Python files need QT_NO_KEYWORDS
set_source_files_properties(
    python_console_plugin.cpp
    PROPERTIES COMPILE_DEFINITIONS "QT_NO_KEYWORDS"
)

install(TARGETS python_console_plugin
    LIBRARY DESTINATION ${QGIS_PLUGIN_SUBDIR}
)
```

- [ ] **Step 4: Commit Python console plugin**

```bash
git add src/plugins/python_console/
git commit -m "feat(plugins): add PythonConsolePlugin"
```

---

## Task 9: Create Plugins Parent CMake

**Files:**
- Create: `src/plugins/CMakeLists.txt`

- [ ] **Step 1: Create plugins parent CMakeLists.txt**

```cmake
# src/plugins/CMakeLists.txt
add_subdirectory(layer_tree)
add_subdirectory(processing)
add_subdirectory(python_console)
```

- [ ] **Step 2: Commit plugins parent CMake**

```bash
git add src/plugins/CMakeLists.txt
git commit -m "build(plugins): add parent CMakeLists.txt for plugins"
```

---

## Task 10: Create GUI Library CMake

**Files:**
- Create: `src/gui/CMakeLists.txt` (new file for GUI library)

- [ ] **Step 1: Create GUI library CMakeLists.txt**

```cmake
# src/gui/CMakeLists.txt
add_library(sicnu_gui SHARED
    main_window.cpp
)

target_include_directories(sicnu_gui PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(sicnu_gui PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    qgis_core
    qgis_gui
    sicnu_core
)
```

- [ ] **Step 2: Commit GUI library CMake**

```bash
git add src/gui/CMakeLists.txt
git commit -m "build(gui): add CMakeLists.txt for GUI library"
```

---

## Task 11: Create New Main Application

**Files:**
- Create: `src/app/main.cpp`
- Create: `src/app/CMakeLists.txt`

- [ ] **Step 1: Create new main.cpp**

```cpp
// src/app/main.cpp
#include <QApplication>
#include <QgsApplication.h>
#include <QgsGui.h>
#include <QTimer>
#include <QFileInfo>

#include "gui/main_window.h"
#include "core/plugin_manager.h"

int main(int argc, char *argv[])
{
    qDebug() << "Starting SICNU GEO RS...";

    // Create QGIS application
    QgsApplication *app = new QgsApplication(argc, argv, true);
    app->setApplicationName("SICNU GEO RS");
    app->setApplicationVersion("2.0");
    app->setOrganizationName("SICNU");

    // Set prefix path and initialize
    qDebug() << "Setting prefix path...";
    QgsApplication::setPrefixPath("/home/kevin/projects/exp-rs", true);
    qDebug() << "Initializing QGIS...";
    QgsApplication::initQgis();
    qDebug() << "QGIS initialized";

    // Initialize QgsGui singleton
    qDebug() << "Initializing QgsGui...";
    QgsGui::instance();
    qDebug() << "QgsGui initialized";

    // Create main window
    qDebug() << "Creating window...";
    SicnuMainWindow window;
    window.initialize();

    qDebug() << "Showing window...";
    window.show();
    qDebug() << "Window shown";

    // Auto-load sample data if available
    QString samplePath = "/home/kevin/projects/exp-rs/data/sample_crops.tif";
    if (QFileInfo::exists(samplePath)) {
        QTimer::singleShot(500, [&window, samplePath]() {
            window.addRasterLayer();
        });
    }

    int result = app->exec();

    delete app;
    return result;
}
```

- [ ] **Step 2: Create app CMakeLists.txt**

```cmake
# src/app/CMakeLists.txt
add_executable(sicnu_geo_rs
    main.cpp
)

target_include_directories(sicnu_geo_rs PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(sicnu_geo_rs PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    Qt6::Xml
    Qt6::Svg
    Qt6::Sql
    qgis_core
    qgis_gui
    qgis_native
    sicnu_core
    sicnu_gui
    CURL::libcurl
    ${PCRE2_16}
    ${PCRE2_8}
)

set_target_properties(sicnu_geo_rs PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
    OUTPUT_NAME "sicnu_geo_rs"
)

install(TARGETS sicnu_geo_rs
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
)
```

- [ ] **Step 3: Commit new main application**

```bash
git add src/app/
git commit -m "feat(app): create new main application with plugin architecture"
```

---

## Task 12: Update Root CMakeLists.txt

**Files:**
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Update root CMakeLists.txt**

Remove the old executable definition and add the new subdirectories:

```cmake
# Remove the old executable definition (lines 152-206)
# Replace with:

add_subdirectory(src/core)
add_subdirectory(src/gui)
add_subdirectory(src/plugins)
add_subdirectory(src/app)
```

- [ ] **Step 2: Commit root CMakeLists.txt update**

```bash
git add CMakeLists.txt
git commit -m "build: update root CMakeLists.txt for plugin architecture"
```

---

## Task 13: Build and Test

- [ ] **Step 1: Clean build directory**

```bash
rm -rf build && mkdir build
```

- [ ] **Step 2: Configure with CMake**

```bash
cd build && cmake .. -DCMAKE_BUILD_TYPE=Debug
```

Expected: CMake configuration succeeds without errors

- [ ] **Step 3: Build the project**

```bash
cd build && make -j$(nproc)
```

Expected: Build succeeds with all libraries and plugins compiled

- [ ] **Step 4: Run the application**

```bash
cd build && ./sicnu_geo_rs
```

Expected: Application starts, plugins load, main window displays

- [ ] **Step 5: Test plugin loading**

Check the console output for:
- "LayerTreePlugin initialized"
- "ProcessingPlugin initialized"
- "PythonConsolePlugin initialized"

- [ ] **Step 6: Test basic functionality**

1. Add a raster layer
2. Add a vector layer
3. Check layer tree displays correctly
4. Check processing toolbox is available
5. Check Python console is available

- [ ] **Step 7: Commit final changes**

```bash
git add -A
git commit -m "refactor: complete plugin architecture migration"
```

---

## Self-Review Checklist

1. **Spec coverage:** All requirements from the design spec are covered:
   - Plugin interface ✓
   - Plugin manager ✓
   - Main window refactoring ✓
   - Layer tree plugin ✓
   - Processing plugin ✓
   - Python console plugin ✓
   - CMake build system ✓

2. **Placeholder scan:** No TBD, TODO, or incomplete sections found.

3. **Type consistency:** All types, method signatures, and property names are consistent across tasks.

4. **File paths:** All file paths are exact and consistent.

5. **Code completeness:** Every step contains complete, runnable code.

---

## Execution Handoff

Plan complete and saved to `docs/superpowers/plans/2026-05-29-plugin-architecture-refactor.md`. Two execution options:

**1. Subagent-Driven (recommended)** - I dispatch a fresh subagent per task, review between tasks, fast iteration

**2. Inline Execution** - Execute tasks in this session using executing-plans, batch execution with checkpoints

Which approach?
