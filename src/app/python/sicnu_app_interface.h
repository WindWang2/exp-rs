// sicnu_app_interface.h — QGIS-modeled QgisInterface facade for SICNU GEO RS
#pragma once

#include "qgisinterface.h"

class QWidget;
class QgsMapCanvas;
class QgsLayerTreeView;
class QgsMessageBar;
class QgsMessageOutput;
class QMenu;
class QToolBar;

class ActiveViewHost;

namespace sicnu::app {
class ProjectContext;
}

/**
 * Concrete implementation of QgisInterface for SICNU GEO RS.
 * Wraps QMainWindow (or main shell), ActiveViewHost, and ProjectContext,
 * exposing standard QGIS iface methods to Python plugins while enforcing
 * the Data/Display seam (ADR 0010).
 */
class SicnuAppInterface : public QgisInterface
{
    Q_OBJECT

public:
    /// Single-seam facade (ADR 0015): GIS shell state flows only through @a activeViewHost.
    /// @a mainWindow remains for plugin menu/toolbar hosting; @a projectContext is optional
    /// for display-manager lookups after openPath.
    explicit SicnuAppInterface( QWidget *mainWindow = nullptr,
                                ActiveViewHost *activeViewHost = nullptr,
                                sicnu::app::ProjectContext *projectContext = nullptr,
                                QObject *parent = nullptr );

    ~SicnuAppInterface() override = default;

    void setActiveViewHost( ActiveViewHost *host )
    {
      m_activeViewHost = host;
    }
    ActiveViewHost *activeViewHost() const { return m_activeViewHost; }

    void setProjectContext( sicnu::app::ProjectContext *context ) { m_projectContext = context; }
    sicnu::app::ProjectContext *projectContext() const { return m_projectContext; }

    // ── QGIS Interface Implementations ─────────────────────────────────
    QgsPluginManagerInterface *pluginManagerInterface() override { return nullptr; }
    QgsLayerTreeView *layerTreeView() override;
    QgsGpsToolsInterface *gpsTools() override { return nullptr; }
    void addCustomActionForLayerType( QAction *action, QString menu, Qgis::LayerType type, bool allLayers ) override { Q_UNUSED(action); Q_UNUSED(menu); Q_UNUSED(type); Q_UNUSED(allLayers); }
    void addCustomActionForLayer( QAction *action, QgsMapLayer *layer ) override { Q_UNUSED(action); Q_UNUSED(layer); }
    bool removeCustomActionForLayerType( QAction *action ) override { Q_UNUSED(action); return false; }
    QList<QgsMapCanvas *> mapCanvases() override;
    QgsMapCanvas *createNewMapCanvas( const QString &name ) override { Q_UNUSED(name); return nullptr; }
    void closeMapCanvas( const QString &name ) override { Q_UNUSED(name); }
    QList<Qgs3DMapCanvas *> mapCanvases3D() override { return {}; }
    Qgs3DMapCanvas *createNewMapCanvas3D( const QString &name, Qgis::SceneMode sceneMode = Qgis::SceneMode::Local ) override { Q_UNUSED(name); Q_UNUSED(sceneMode); return nullptr; }
    void closeMapCanvas3D( const QString &name ) override { Q_UNUSED(name); }
    QSize iconSize( bool dockedToolbar = false ) const override { Q_UNUSED(dockedToolbar); return QSize( 24, 24 ); }
    QList<QgsMapLayer *> editableLayers( bool modified = false ) const override { Q_UNUSED(modified); return {}; }
    QgsMapLayer *activeLayer() override;
    QgsMapCanvas *mapCanvas() override;
    QList<QgsMapDecoration *> activeDecorations() override { return {}; }
    QgsLayerTreeMapCanvasBridge *layerTreeCanvasBridge() override { return nullptr; }
    QWidget *mainWindow() override;
    QgsMessageBar *messageBar() override;
    QList<QgsLayoutDesignerInterface *> openLayoutDesigners() override { return {}; }
    QMap<QString, QVariant> defaultStyleSheetOptions() override { return {}; }
    QFont defaultStyleSheetFont() override;
    QgsAdvancedDigitizingDockWidget *cadDockWidget() override { return nullptr; }

    QMenu *projectMenu() override { return nullptr; }
    QMenu *projectImportExportMenu() override { return nullptr; }
    void addProjectImportAction( QAction *action ) override { Q_UNUSED(action); }
    void removeProjectImportAction( QAction *action ) override { Q_UNUSED(action); }
    void addProjectExportAction( QAction *action ) override { Q_UNUSED(action); }
    void removeProjectExportAction( QAction *action ) override { Q_UNUSED(action); }
    QMenu *projectModelsMenu() override { return nullptr; }
    QMenu *createProjectModelSubMenu( const QString &title ) override { Q_UNUSED(title); return nullptr; }
    QMenu *editMenu() override { return nullptr; }
    QMenu *viewMenu() override { return nullptr; }
    QMenu *layerMenu() override { return nullptr; }
    QMenu *newLayerMenu() override { return nullptr; }
    QMenu *addLayerMenu() override { return nullptr; }
    QMenu *settingsMenu() override { return nullptr; }
    QMenu *pluginMenu() override;
    QMenu *pluginHelpMenu() override { return nullptr; }
    QMenu *rasterMenu() override { return nullptr; }
    QMenu *databaseMenu() override { return nullptr; }
    QMenu *vectorMenu() override { return nullptr; }
    QMenu *webMenu() override { return nullptr; }
    QMenu *meshMenu() override { return nullptr; }
    QMenu *firstRightStandardMenu() override { return nullptr; }
    QMenu *windowMenu() override { return nullptr; }
    QMenu *helpMenu() override { return nullptr; }

    QToolBar *fileToolBar() override { return nullptr; }
    QToolBar *layerToolBar() override { return nullptr; }
    QToolBar *dataSourceManagerToolBar() override { return nullptr; }
    void openDataSourceManagerPage( const QString &pageName ) override { Q_UNUSED(pageName); }
    QToolBar *mapNavToolToolBar() override { return nullptr; }
    QToolBar *digitizeToolBar() override { return nullptr; }
    QToolBar *advancedDigitizeToolBar() override { return nullptr; }
    QToolBar *shapeDigitizeToolBar() override { return nullptr; }
    QToolBar *attributesToolBar() override { return nullptr; }
    QToolBar *selectionToolBar() override { return nullptr; }
    QToolBar *pluginToolBar() override;
    QToolBar *helpToolBar() override { return nullptr; }
    QToolBar *rasterToolBar() override { return nullptr; }
    QToolBar *vectorToolBar() override { return nullptr; }
    QToolBar *databaseToolBar() override { return nullptr; }
    QToolBar *webToolBar() override { return nullptr; }

    QgsMessageOutput *messageOutput() { return nullptr; }

    // Action Getters Stubs
    QAction *actionNewProject() override { return nullptr; }
    QAction *actionOpenProject() override { return nullptr; }
    QAction *actionSaveProject() override { return nullptr; }
    QAction *actionSaveProjectAs() override { return nullptr; }
    QAction *actionSaveMapAsImage() override { return nullptr; }
    QAction *actionProjectProperties() override { return nullptr; }
    QAction *actionCreatePrintLayout() override { return nullptr; }
    QAction *actionShowLayoutManager() override { return nullptr; }
    QAction *actionExit() override { return nullptr; }
    QAction *actionCutFeatures() override { return nullptr; }
    QAction *actionCopyFeatures() override { return nullptr; }
    QAction *actionPasteFeatures() override { return nullptr; }
    QAction *actionAddFeature() override { return nullptr; }
    QAction *actionDeleteSelected() override { return nullptr; }
    QAction *actionMoveFeature() override { return nullptr; }
    QAction *actionSplitFeatures() override { return nullptr; }
    QAction *actionSplitParts() override { return nullptr; }
    QAction *actionAddRing() override { return nullptr; }
    QAction *actionAddPart() override { return nullptr; }
    QAction *actionSimplifyFeature() override { return nullptr; }
    QAction *actionDeleteRing() override { return nullptr; }
    QAction *actionDeletePart() override { return nullptr; }
    QAction *actionVertexTool() override { return nullptr; }
    QAction *actionVertexToolActiveLayer() override { return nullptr; }
    QActionGroup *mapToolActionGroup() override { return nullptr; }
    QAction *actionPan() override { return nullptr; }
    QAction *actionPanToSelected() override { return nullptr; }
    QAction *actionZoomIn() override { return nullptr; }
    QAction *actionZoomOut() override { return nullptr; }
    QAction *actionSelect() override { return nullptr; }
    QAction *actionSelectRectangle() override { return nullptr; }
    QAction *actionSelectPolygon() override { return nullptr; }
    QAction *actionSelectFreehand() override { return nullptr; }
    QAction *actionSelectRadius() override { return nullptr; }
    QAction *actionIdentify() override { return nullptr; }
    QAction *actionFeatureAction() override { return nullptr; }
    QAction *actionMeasure() override { return nullptr; }
    QAction *actionMeasureArea() override { return nullptr; }
    QAction *actionZoomFullExtent() override { return nullptr; }
    QAction *actionZoomToLayer() override { return nullptr; }
    QAction *actionZoomToLayers() override { return nullptr; }
    QAction *actionZoomToSelected() override { return nullptr; }
    QAction *actionZoomLast() override { return nullptr; }
    QAction *actionZoomNext() override { return nullptr; }
    QAction *actionZoomActualSize() override { return nullptr; }
    QAction *actionMapTips() override { return nullptr; }
    QAction *actionNewBookmark() override { return nullptr; }
    QAction *actionShowBookmarks() override { return nullptr; }
    QAction *actionDraw() override { return nullptr; }
    QAction *actionNewVectorLayer() override { return nullptr; }
    QAction *actionAddOgrLayer() override { return nullptr; }
    QAction *actionAddRasterLayer() override { return nullptr; }
    QAction *actionAddPgLayer() override { return nullptr; }
    QAction *actionAddWmsLayer() override { return nullptr; }
    QAction *actionAddXyzLayer() override { return nullptr; }
    QAction *actionAddVectorTileLayer() override { return nullptr; }
    QAction *actionAddPointCloudLayer() override { return nullptr; }
    QAction *actionAddAfsLayer() override { return nullptr; }
    QAction *actionAddAmsLayer() override { return nullptr; }
    QAction *actionCopyLayerStyle() override { return nullptr; }
    QAction *actionPasteLayerStyle() override { return nullptr; }
    QAction *actionOpenTable() override { return nullptr; }
    QAction *actionOpenFieldCalculator() override { return nullptr; }
    QAction *actionOpenStatisticalSummary() override { return nullptr; }
    QAction *actionToggleEditing() override { return nullptr; }
    QAction *actionSaveActiveLayerEdits() override { return nullptr; }
    QAction *actionAllEdits() override { return nullptr; }
    QAction *actionSaveEdits() override { return nullptr; }
    QAction *actionSaveAllEdits() override { return nullptr; }
    QAction *actionRollbackEdits() override { return nullptr; }
    QAction *actionRollbackAllEdits() override { return nullptr; }
    QAction *actionCancelEdits() override { return nullptr; }
    QAction *actionCancelAllEdits() override { return nullptr; }
    QAction *actionLayerSaveAs() override { return nullptr; }
    QAction *actionDuplicateLayer() override { return nullptr; }
    QAction *actionLayerProperties() override { return nullptr; }
    QAction *actionAddToOverview() override { return nullptr; }
    QAction *actionAddAllToOverview() override { return nullptr; }
    QAction *actionRemoveAllFromOverview() override { return nullptr; }
    QAction *actionHideAllLayers() override { return nullptr; }
    QAction *actionShowAllLayers() override { return nullptr; }
    QAction *actionHideSelectedLayers() override { return nullptr; }
    QAction *actionToggleSelectedLayers() override { return nullptr; }
    QAction *actionToggleSelectedLayersIndependently() override { return nullptr; }
    QAction *actionHideDeselectedLayers() override { return nullptr; }
    QAction *actionShowSelectedLayers() override { return nullptr; }
    QAction *actionManagePlugins() override { return nullptr; }
    QAction *actionPluginListSeparator() override { return nullptr; }
    QAction *actionShowPythonDialog() override { return nullptr; }
    QAction *actionToggleFullScreen() override { return nullptr; }
    QAction *actionOptions() override { return nullptr; }
    QAction *actionCustomProjection() override { return nullptr; }
    QAction *actionHelpContents() override { return nullptr; }
    QAction *actionQgisHomePage() override { return nullptr; }
    QAction *actionCheckQgisVersion() override { return nullptr; }
    QAction *actionAbout() override { return nullptr; }

    QgsVectorLayerTools *vectorLayerTools() override { return nullptr; }
    int messageTimeout() override { return 5; }
    QgsStatusBar *statusBarIface() override { return nullptr; }
    QgsLayerTreeRegistryBridge::InsertionPoint layerTreeInsertionPoint() override { return QgsLayerTreeRegistryBridge::InsertionPoint( nullptr, 0 ); }
    QgsUserProfileManager *userProfileManager() override { return nullptr; }
    void zoomFull() override {}
    void zoomToPrevious() override {}
    void zoomToNext() override {}
    void zoomToActiveLayer() override {}
    void copySelectionToClipboard( QgsMapLayer *l ) override { Q_UNUSED(l); }
    void pasteFromClipboard( QgsMapLayer *l ) override { Q_UNUSED(l); }
    void openMessageLog( const QString &tabName = QString() ) override { Q_UNUSED(tabName); }
    void addUserInputWidget( QWidget *widget ) override { Q_UNUSED(widget); }
    void showLayoutManager() override {}
    QgsLayoutDesignerInterface *openLayoutDesigner( QgsMasterLayoutInterface *layout ) override { Q_UNUSED(layout); return nullptr; }
    void showOptionsDialog( QWidget *parent = nullptr, const QString &currentPage = QString() ) override { Q_UNUSED(parent); Q_UNUSED(currentPage); }
    void showProjectPropertiesDialog( const QString &currentPage = QString() ) override { Q_UNUSED(currentPage); }
    void buildStyleSheet( const QMap<QString, QVariant> &opts ) override { Q_UNUSED(opts); }
    void saveStyleSheetOptions( const QMap<QString, QVariant> &opts ) override { Q_UNUSED(opts); }
    void insertAddLayerAction( QAction *action ) override { Q_UNUSED(action); }
    void removeAddLayerAction( QAction *action ) override { Q_UNUSED(action); }

    // Layer addition — routes to ActiveViewHost (ADR 0010 compliant)
    QgsRasterLayer *addRasterLayer( const QString &rasterLayerPath, const QString &baseName = QString() ) override;
    QgsRasterLayer *addRasterLayer( const QString &url, const QString &layerName, const QString &providerKey ) override { Q_UNUSED(providerKey); return addRasterLayer(url, layerName); }
    QgsMeshLayer *addMeshLayer( const QString &url, const QString &baseName, const QString &providerKey ) override { Q_UNUSED(url); Q_UNUSED(baseName); Q_UNUSED(providerKey); return nullptr; }
    QgsVectorTileLayer *addVectorTileLayer( const QString &url, const QString &baseName ) override { Q_UNUSED(url); Q_UNUSED(baseName); return nullptr; }
    QgsPointCloudLayer *addPointCloudLayer( const QString &url, const QString &baseName, const QString &providerKey ) override { Q_UNUSED(url); Q_UNUSED(baseName); Q_UNUSED(providerKey); return nullptr; }
    QgsTiledSceneLayer *addTiledSceneLayer( const QString &url, const QString &baseName, const QString &providerKey ) override { Q_UNUSED(url); Q_UNUSED(baseName); Q_UNUSED(providerKey); return nullptr; }
    bool addProject( const QString &project ) override { Q_UNUSED(project); return false; }
    bool newProject( bool promptToSaveFlag = false ) override { Q_UNUSED(promptToSaveFlag); return false; }
    void reloadConnections() override {}
    bool setActiveLayer( QgsMapLayer *layer ) override { Q_UNUSED(layer); return false; }

    QgsVectorLayer *addVectorLayer( const QString &vectorLayerPath, const QString &baseName, const QString &providerKey = QStringLiteral( "ogr" ) ) override;

    // Menu and Toolbar plugin contributions
    void addPluginToMenu( const QString &name, QAction *action ) override;
    void removePluginMenu( const QString &name, QAction *action ) override;

    void addPluginToDatabaseMenu( const QString &name, QAction *action ) override { Q_UNUSED(name); Q_UNUSED(action); }
    void removePluginDatabaseMenu( const QString &name, QAction *action ) override { Q_UNUSED(name); Q_UNUSED(action); }

    void addPluginToRasterMenu( const QString &name, QAction *action ) override { Q_UNUSED(name); Q_UNUSED(action); }
    void removePluginRasterMenu( const QString &name, QAction *action ) override { Q_UNUSED(name); Q_UNUSED(action); }

    void addPluginToVectorMenu( const QString &name, QAction *action ) override { Q_UNUSED(name); Q_UNUSED(action); }
    void removePluginVectorMenu( const QString &name, QAction *action ) override { Q_UNUSED(name); Q_UNUSED(action); }

    void addPluginToWebMenu( const QString &name, QAction *action ) override { Q_UNUSED(name); Q_UNUSED(action); }
    void removePluginWebMenu( const QString &name, QAction *action ) override { Q_UNUSED(name); Q_UNUSED(action); }

    void addPluginToMeshMenu( const QString &name, QAction *action ) override { Q_UNUSED(name); Q_UNUSED(action); }
    void removePluginMeshMenu( const QString &name, QAction *action ) override { Q_UNUSED(name); Q_UNUSED(action); }

    int addToolBarIcon( QAction *action ) override;
    QAction *addToolBarWidget( QWidget *widget ) override;
    void removeToolBarIcon( QAction *action ) override;
    int addRasterToolBarIcon( QAction *action ) override { Q_UNUSED(action); return -1; }
    QAction *addRasterToolBarWidget( QWidget *widget ) override { Q_UNUSED(widget); return nullptr; }
    void removeRasterToolBarIcon( QAction *action ) override { Q_UNUSED(action); }
    int addVectorToolBarIcon( QAction *action ) override { Q_UNUSED(action); return -1; }
    QAction *addVectorToolBarWidget( QWidget *widget ) override { Q_UNUSED(widget); return nullptr; }
    void removeVectorToolBarIcon( QAction *action ) override { Q_UNUSED(action); }
    int addDatabaseToolBarIcon( QAction *action ) override { Q_UNUSED(action); return -1; }
    QAction *addDatabaseToolBarWidget( QWidget *widget ) override { Q_UNUSED(widget); return nullptr; }
    void removeDatabaseToolBarIcon( QAction *action ) override { Q_UNUSED(action); }
    int addWebToolBarIcon( QAction *action ) override { Q_UNUSED(action); return -1; }
    QAction *addWebToolBarWidget( QWidget *widget ) override { Q_UNUSED(widget); return nullptr; }
    void removeWebToolBarIcon( QAction *action ) override { Q_UNUSED(action); }

    QToolBar *addToolBar( const QString &name ) override { Q_UNUSED(name); return nullptr; }
    void addToolBar( QToolBar *toolbar, Qt::ToolBarArea area = Qt::TopToolBarArea ) override { Q_UNUSED(toolbar); Q_UNUSED(area); }
    void addDockWidget( Qt::DockWidgetArea area, QDockWidget *dockWidget ) override;
    void addTabifiedDockWidget( Qt::DockWidgetArea area, QDockWidget *dockwidget, const QStringList &tabifyWith = QStringList(), bool raiseTab = false ) override { Q_UNUSED(area); Q_UNUSED(dockwidget); Q_UNUSED(tabifyWith); Q_UNUSED(raiseTab); }
    void removeDockWidget( QDockWidget *dockWidget ) override;

    void showLayerProperties( QgsMapLayer *l, const QString &page = QString() ) override { Q_UNUSED(l); Q_UNUSED(page); }
    QDialog *showAttributeTable( QgsVectorLayer *l, const QString &filterExpression = QString() ) override { Q_UNUSED(l); Q_UNUSED(filterExpression); return nullptr; }
    void addWindow( QAction *action ) override { Q_UNUSED(action); }
    void removeWindow( QAction *action ) override { Q_UNUSED(action); }
    bool registerMainWindowAction( QAction *action, const QString &defaultShortcut ) override { Q_UNUSED(action); Q_UNUSED(defaultShortcut); return false; }
    bool unregisterMainWindowAction( QAction *action ) override { Q_UNUSED(action); return false; }
    void registerMapLayerConfigWidgetFactory( QgsMapLayerConfigWidgetFactory *factory ) override { Q_UNUSED(factory); }
    void unregisterMapLayerConfigWidgetFactory( QgsMapLayerConfigWidgetFactory *factory ) override { Q_UNUSED(factory); }
    void registerOptionsWidgetFactory( QgsOptionsWidgetFactory *factory ) override { Q_UNUSED(factory); }
    void unregisterOptionsWidgetFactory( QgsOptionsWidgetFactory *factory ) override { Q_UNUSED(factory); }
    void registerProjectPropertiesWidgetFactory( QgsOptionsWidgetFactory *factory ) override { Q_UNUSED(factory); }
    void unregisterProjectPropertiesWidgetFactory( QgsOptionsWidgetFactory *factory ) override { Q_UNUSED(factory); }
    void registerDevToolWidgetFactory( QgsDevToolWidgetFactory *factory ) override { Q_UNUSED(factory); }
    void unregisterDevToolWidgetFactory( QgsDevToolWidgetFactory *factory ) override { Q_UNUSED(factory); }
    void showApiDocumentation( Qgis::DocumentationApi api = Qgis::DocumentationApi::PyQgis, Qgis::DocumentationBrowser browser = Qgis::DocumentationBrowser::DeveloperToolsPanel, const QString &object = QString(), const QString &module = QString() ) override { Q_UNUSED(api); Q_UNUSED(browser); Q_UNUSED(object); Q_UNUSED(module); }
    void registerApplicationExitBlocker( QgsApplicationExitBlockerInterface *blocker ) override { Q_UNUSED(blocker); }
    void unregisterApplicationExitBlocker( QgsApplicationExitBlockerInterface *blocker ) override { Q_UNUSED(blocker); }
    void registerMapToolHandler( QgsAbstractMapToolHandler *handler ) override { Q_UNUSED(handler); }
    void unregisterMapToolHandler( QgsAbstractMapToolHandler *handler ) override { Q_UNUSED(handler); }
    void registerCustomDropHandler( QgsCustomDropHandler *handler ) override { Q_UNUSED(handler); }
    void unregisterCustomDropHandler( QgsCustomDropHandler *handler ) override { Q_UNUSED(handler); }
    void registerCustomProjectOpenHandler( QgsCustomProjectOpenHandler *handler ) override { Q_UNUSED(handler); }
    void unregisterCustomProjectOpenHandler( QgsCustomProjectOpenHandler *handler ) override { Q_UNUSED(handler); }
    void registerCustomLayoutDropHandler( QgsLayoutCustomDropHandler *handler ) override { Q_UNUSED(handler); }
    void unregisterCustomLayoutDropHandler( QgsLayoutCustomDropHandler *handler ) override { Q_UNUSED(handler); }
    void openURL( const QString &url, bool useQgisDocDirectory = true ) override { Q_UNUSED(url); Q_UNUSED(useQgisDocDirectory); }
    bool openFeatureForm( QgsVectorLayer *l, QgsFeature &f, bool updateFeatureOnly = false, bool showModal = true ) override { Q_UNUSED(l); Q_UNUSED(f); Q_UNUSED(updateFeatureOnly); Q_UNUSED(showModal); return false; }
    QgsAttributeDialog *getFeatureForm( QgsVectorLayer *l, QgsFeature &f ) override { Q_UNUSED(l); Q_UNUSED(f); return nullptr; }
    void preloadForm( const QString &uifile ) override { Q_UNUSED(uifile); }
    void locatorSearch( const QString &searchText ) override { Q_UNUSED(searchText); }
    void registerLocatorFilter( QgsLocatorFilter *filter ) override { Q_UNUSED(filter); }
    void deregisterLocatorFilter( QgsLocatorFilter *filter ) override { Q_UNUSED(filter); }
    void invalidateLocatorResults() override {}
    bool askForDatumTransform( QgsCoordinateReferenceSystem sourceCrs, QgsCoordinateReferenceSystem destinationCrs ) override { Q_UNUSED(sourceCrs); Q_UNUSED(destinationCrs); return false; }
    QgsBrowserGuiModel *browserModel() override { return nullptr; }
    void setGpsPanelConnection( QgsGpsConnection *connection ) override { Q_UNUSED(connection); }
    void blockActiveLayerChanges( bool blocked ) override { Q_UNUSED(blocked); }

private:
    QWidget *m_mainWindow = nullptr;
    ActiveViewHost *m_activeViewHost = nullptr;
    sicnu::app::ProjectContext *m_projectContext = nullptr;

    QMenu *m_pluginMenu = nullptr;
    QToolBar *m_pluginToolBar = nullptr;
};
