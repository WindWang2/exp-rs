#pragma once

#include <QMainWindow>
#include <QMap>
#include <QDomDocument>

#include <qgspointxy.h>
#include <qgscoordinatereferencesystem.h>

class QgsMapCanvas;
class QgsLayerTreeView;
class QgsLayerTreeModel;
class QgsLayerTreeGroup;
class QgsMapLayer;
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
