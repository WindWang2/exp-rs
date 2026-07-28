#pragma once

#include <QMainWindow>
#include <QLabel>
#include <QHBoxLayout>
#include <QCloseEvent>
#include <QSettings>
#include <QElapsedTimer>

// QGIS includes
#include <qgsmapcanvas.h>
#include <qgslayertreeview.h>
#include <qgslayertreemodel.h>
#include <qgsprojectionselectionwidget.h>
#include <qgsprocessingtoolboxtreeview.h>
#include <qgsmaptoolpan.h>
#include <qgsmaptoolzoom.h>
#include <qgsmaptoolidentify.h>
#include <qgsmapmouseevent.h>
#include <qgspointxy.h>
#include <qgsdockwidget.h>
#include <qgsmapoverviewcanvas.h>

#include "map_tools/measure_tool.h"

class QDomDocument;
class QMenuBar;
class QSlider;
class QToolBar;
class QVBoxLayout;
class ActiveViewHost;
class QSplitter;
class SecondaryMapViewWidget;
class QPainter;

#include "display/qgis_display_manager.h"
class QTextBrowser;
class LayerTreeMenuProvider;
class QgsBrowserDockWidget;
class QgsBrowserGuiModel;
class SpectralProfileWidget;
class QgsGeoreferencerMainWindow;
class QgsGeorefImageToMapWindow;
class QgsClassificationMainWindow;
class QgsAdvancedDigitizingDockWidget;
class QgsMessageBar;
class QgsMapToolSelect;
class QgsMapToolAddFeature;
class QgsMapToolMoveFeature;
class QgsMapToolRotateFeature;
class QgsMapToolScaleFeature;

namespace sicnu {

class DataManagerPanel;
namespace data {
class AssetId;
}
namespace app {
class ProjectContext;
}
}
class QgsMapToolOffsetCurve;
class QgsMapToolReshape;
class QgsMapToolSplitFeatures;
class QgsMapToolSplitParts;
class QgsMapToolSimplify;
class QgsMapToolReverseLine;
class QgsMapToolAddRing;
class QgsMapToolAddPart;
class QgsMapToolFillRing;
class QgsMapToolDeletePart;
class QgsMapToolDeleteRing;
class QgsMapToolTrimExtendFeature;
class QgsMapToolChamferFillet;
class QgsMapToolFeatureArray;
class QgsVertexTool;
class QgsFeatureAction;
class QgsClipboard;
class QgsAttributeTableDialog;
class QgsVectorLayer;
class SwipeMapTool;
class TaskPanelHost;
class WorkflowSessionController;
class RibbonController;
class RsJobPanel;

namespace Sicnu { class PythonScriptEditor; }

/**
 * \brief Custom identify tool that emits results as a signal.
 *
 * Overrides canvasReleaseEvent to call identify() and emit
 * identifyCompleted with the results list.
 */
class CustomIdentifyTool : public QgsMapToolIdentify
{
    Q_OBJECT

  public:
    explicit CustomIdentifyTool( QgsMapCanvas *canvas )
        : QgsMapToolIdentify( canvas )
    {
    }

    void canvasReleaseEvent( QgsMapMouseEvent *e ) override
    {
        m_lastClickedPoint = toMapCoordinates( e->pos() );
        QList<QgsMapToolIdentify::IdentifyResult> results = identify( e->x(), e->y(), TopDownStopAtFirst );
        emit identifyCompleted( results );
    }

    QgsPointXY lastClickedPoint() const { return m_lastClickedPoint; }

  signals:
    void identifyCompleted( const QList<QgsMapToolIdentify::IdentifyResult> &results );

  private:
    QgsPointXY m_lastClickedPoint;
};

class QgisDesktopWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit QgisDesktopWindow(QWidget *parent = nullptr);
    ~QgisDesktopWindow() override;

    void setupUi();
    void setupMapCanvas();
    void initLayerTree();

    /**
     * Detached QMenuBar used only as an action/shortcut host.
     * Must NOT use QMainWindow::menuBar() after setMenuWidget(chrome) —
     * menuBar() recreates a QMenuBar and deletes the top chrome.
     */
    QMenuBar *appMenuBar();

    /**
     * QGIS-style panel/toolbar visibility menu (checkable toggles).
     * Used by ribbon right-click and QMainWindow empty-area popup.
     * Caller owns the returned menu (prefer WA_DeleteOnClose + popup).
     */
    QMenu *createPopupMenu() override;

    QgsMapCanvas *mapCanvas() const { return m_mapCanvas; }
    QgsMapLayer *activeLayer();
    QList<QgsMapLayer*> selectedLayers();
    /**
     * Load a local raster/vector source through the project Data Context
     * (registers a Data Asset and adds a main-view Display Layer). Returns true
     * on success. Loading errors are reported through the UI shell.
     */
    bool loadDataLayer(const QString &filePath);

  public slots:
    void addRasterLayer();
    void addVectorLayer();
    void newProject();
    void openProject();
    void saveProject();
    void saveProjectAs();
    void importLayer();
    void openLandsatImportDialog();
    void browseStacCatalog();
    void newLayout();
    void exportLabReport();
    void undo();
    void redo();
    void cutFeatures();
    void copyFeatures();
    void pasteFeatures();
    void selectAll();
    void zoomIn();
    void zoomOut();
    void zoomFullExtent();
    void panMap();
    void identifyFeatures();
    void refreshMap();
    void layerProperties();
    void removeLayer();
    void about();
    void helpContents();
    void checkVersion();
    void loadSampleData();
    void showGuidedWorkflows();
    void options();
    void showProcessingToolbox();
    void showProcessingHistory();

    // Raster processing dialogs
    void openImageEnhancementPanel();
    void openBandMathDialog();
    void openSpectralIndexDialog();
    void openAtmosphericCorrectionDialog();
    void openMosaicDialog();
    void openChangeDetectionDialog();

    // Enhancement dialogs (processing — write new raster)
    void openContrastStretchDialog();
    /** Display-only stretch (layer renderer / symbology), no export. */
    void openDisplayStretchPanel();
    void openSpatialFilterDialog();
    void openPcaDialog();
    void openBandRatioDialog();
    void openSpeckleFilterDialog();
    void openTerrainDialog();
    void openFusionDialog();

    // CRS preset dialog
    void openCrsPresetDialog();
    void setLayerCrsFromPreset();

    // Measurement tools
    void measureDistance();
    void measureArea();

    // Comparison tool
    void openComparisonDialog();
    void toggleSwipeTool();

    // Batch processing
    void openBatchProcessingDialog();

    // Vector editing tools
    void toggleEditing();
    void saveEdits();
    void newVectorLayer();
    void addFeature();
    void moveFeature();
    void rotateFeature();
    void scaleFeature();
    void offsetCurve();
    void reshapeGeometry();
    void splitFeatures();
    void splitParts();
    void simplifyFeature();
    void reverseLine();
    void addRing();
    void addPart();
    void fillRing();
    void deletePart();
    void deleteRing();
    void trimExtendFeature();
    void chamferFillet();
    void featureArray();
    void vertexTool();
    void selectFeatures();
    void openAttributeTable();
    void deleteSelectedFeatures();

    // Image Registration — dual shells (I2I / I2M)
    void openGeoreferencer(); ///< Compatibility alias → openGeorefImageToImage()
    void openGeorefImageToImage();
    void openGeorefImageToMap();
    /** Show / raise the Data Manager catalog dock (left, tabified with Layers). */
    void showDataManagerPanel();

    // Classification (Phase 10A Task 10.2)
    void openClassificationWindow();

    // OBIA Classification (Phase 10B Task 10B.5)
    void openObiaWindow();

    // Multi-view shell (Wave D) — secondary Display View chrome
    void toggleSecondaryMapView( bool on );
    void openSecondaryMapView();
    void closeSecondaryMapView();
    void activateMainMapView();
    void activateSecondaryMapView();
    void syncMainLayersToSecondaryView();

    // Layer loading (public for template helper in main_window_processing.cpp)
    void loadRasterLayer(const QString &filePath);
    void loadVectorLayer(const QString &filePath);

    /** Place product toolbars under the ribbon (max two rows). */
    void layoutToolbarsUnderRibbon();

private slots:
    void onProjectRead(const QDomDocument &doc);
    void onProjectWrite(QDomDocument &doc);
    void onRenderComplete(QPainter *painter);
    void onLayerTreeClicked(const QModelIndex &index);
    void onLayerTreeDoubleClicked(const QModelIndex &index);
    void onCrsChanged(const QgsCoordinateReferenceSystem &crs);
    void onIdentifyResults(const QList<QgsMapToolIdentify::IdentifyResult> &results);
    void showCoordinates(const QgsPointXY &point);
    void updateScale();
    void updateExtents();
    void updateCrsDisplay();
    void setProjectCrs();
    void zoomToLayer();

private:
    void setupMenu();
    void setupToolbars();
    void setupDockWidgets();
    /// Create Data Manager dock after ProjectContext exists (needs DataManager*).
    void setupDataManagerPanel();
    void setupRibbonAndTaskPanel();
    void setupStatusBar();
    void setupConnections();
    void openWorkflowTool( const QString &definitionId );
    void refreshWorkflowLayerChoices();

    QgsLayerTreeGroup *findOrCreateGroup(const QString &name);
    QgsVectorLayer *currentVectorLayer();
    void updateEditingUI(QgsVectorLayer *vlayer);
    void showLayerProperties(QgsMapLayer *layer);
    void refreshCanvasLayers();
    void openProcessingAlgorithm(const QString &algorithmId);

    // Panel state persistence
    void savePanelState();
    void restorePanelState();
    void resetPanelLayout();
    /** Hide chrome that duplicates the Ribbon + Task panel product shell. */
    void applyProductShellLayout();

    bool confirmSaveEdits(QgsVectorLayer *vl);
    bool checkUnsavedChanges();
    /** Acquire the exclusive Edit Lease for an Asset-backed vector layer. */
    bool acquireEditLease(QgsVectorLayer *vlayer, bool showConflictWarning = true);
    /** Commit the Edit Lease (advances Asset Revision, refreshes other layers). */
    void commitEditLease(QgsVectorLayer *vlayer);
    /** Release the Edit Lease without advancing the revision. */
    void rollbackEditLease(QgsVectorLayer *vlayer);
    /** Set every Display Layer of an asset read-only except the edit owner. */
    void setAssetLayersReadOnly(const sicnu::data::AssetId &assetId,
                                QgsVectorLayer *exceptLayer, bool readOnly);
    void applyDarkPalette();
    /** Apply light/dark Canopy Lab theme (Fusion + QSS). */
    void applyUiTheme( const QString &theme );

    // QGIS C++ components
    QgsMapCanvas *m_mapCanvas = nullptr;
    QgsLayerTreeView *m_layerTreeView = nullptr;
    QgsLayerTreeModel *m_layerTreeModel = nullptr;
    QWidget *m_mapCanvasContainer = nullptr;
    QSplitter *m_mapSplitter = nullptr;
    class SecondaryMapViewWidget *m_secondaryMapView = nullptr;
    sicnu::display::DisplayViewId m_secondaryViewId;
    /// Session windows registered as secondary Display Views (Wave E).
    sicnu::display::DisplayViewId m_classifyViewId;
    sicnu::display::DisplayViewId m_obiaViewId;
    sicnu::display::DisplayViewId m_georefI2ISrcViewId;
    sicnu::display::DisplayViewId m_georefI2IDstViewId;
    sicnu::display::DisplayViewId m_georefI2MSrcViewId;
    QAction *m_secondaryViewAction = nullptr;
    QgsProjectionSelectionWidget *m_crsSelector = nullptr;
    LayerTreeMenuProvider *m_layerTreeMenuProvider = nullptr;
    QgsProcessingToolboxTreeView *m_toolboxView = nullptr;

    // Map tools
    QgsMapToolPan *m_panTool = nullptr;
    QgsMapToolZoom *m_zoomInTool = nullptr;
    QgsMapToolZoom *m_zoomOutTool = nullptr;
    CustomIdentifyTool *m_identifyTool = nullptr;
    MeasureTool *m_measureDistanceTool = nullptr;
    MeasureTool *m_measureAreaTool = nullptr;
    SwipeMapTool *m_swipeTool = nullptr;

    // Vector editing map tools
    QgsMapToolSelect *m_selectTool = nullptr;
    QgsMapToolAddFeature *m_addFeatureTool = nullptr;
    QgsMapToolMoveFeature *m_moveFeatureTool = nullptr;
    QgsMapToolRotateFeature *m_rotateFeatureTool = nullptr;
    QgsMapToolScaleFeature *m_scaleFeatureTool = nullptr;
    QgsMapToolOffsetCurve *m_offsetCurveTool = nullptr;
    QgsMapToolReshape *m_reshapeTool = nullptr;
    QgsMapToolSplitFeatures *m_splitFeaturesTool = nullptr;
    QgsMapToolSplitParts *m_splitPartsTool = nullptr;
    QgsMapToolSimplify *m_simplifyTool = nullptr;
    QgsMapToolReverseLine *m_reverseLineTool = nullptr;
    QgsMapToolAddRing *m_addRingTool = nullptr;
    QgsMapToolAddPart *m_addPartTool = nullptr;
    QgsMapToolFillRing *m_fillRingTool = nullptr;
    QgsMapToolDeletePart *m_deletePartTool = nullptr;
    QgsMapToolDeleteRing *m_deleteRingTool = nullptr;
    QgsMapToolTrimExtendFeature *m_trimExtendTool = nullptr;
    QgsMapToolChamferFillet *m_chamferFilletTool = nullptr;
    QgsMapToolFeatureArray *m_featureArrayTool = nullptr;
    QgsVertexTool *m_vertexTool = nullptr;

    // Vector editing infrastructure
    QgsAdvancedDigitizingDockWidget *m_cadDock = nullptr;
    QgsMessageBar *m_messageBar = nullptr;
    QgsClipboard *m_clipboard = nullptr;
    QAction *m_toggleEditingAction = nullptr;
    QAction *m_saveEditsAction = nullptr;
    QList<QAction *> m_editingToolActions;

    // Dock widgets
    QgsDockWidget *m_layersDock = nullptr;
    QgsBrowserDockWidget *m_browserDock = nullptr;
    QgsBrowserGuiModel *m_browserModel = nullptr;
    QgsDockWidget *m_processingDock = nullptr;
    QgsDockWidget *m_overviewDock = nullptr;
    QgsMapOverviewCanvas *m_overviewCanvas = nullptr;
    QgsDockWidget *m_identifyDock = nullptr;
    QgsDockWidget *m_spectralDock = nullptr;
    QgsDockWidget *m_histogramStretchDock = nullptr;
    QgsDockWidget *m_logDock = nullptr;
    RsJobPanel *m_jobPanel = nullptr;
    QgsDockWidget *m_workflowDock = nullptr;
    QgsDockWidget *m_taskPanelDock = nullptr;

    sicnu::DataManagerPanel *m_dataManagerPanel = nullptr;
    TaskPanelHost *m_taskPanel = nullptr;
    WorkflowSessionController *m_sessionController = nullptr;
    RibbonController *m_ribbonController = nullptr;
    QWidget *m_ribbonBar = nullptr;
    QWidget *m_topChrome = nullptr;
    /** Host under ribbon for product toolbars (never QMainWindow top area). */
    QWidget *m_toolbarStrip = nullptr;
    class RsToolbarFlowHost *m_toolbarFlowHost = nullptr;
    /** Guard against re-entrant layout (show/hide syncs toggleViewAction → loop). */
    bool m_layoutingToolbarsUnderRibbon = false;
    QToolBar *m_mapToolsToolBar = nullptr;
    QToolBar *m_digitizeToolBar = nullptr;
    QMenuBar *m_hiddenMenuBar = nullptr;
    QMenu *m_windowMenu = nullptr;

    // Identify results display
    QTextBrowser *m_identifyResults = nullptr;

    // Spectral profile display
    SpectralProfileWidget *m_spectralProfile = nullptr;

    // Histogram stretch display
    class HistogramStretchWidget *m_histogramStretch = nullptr;

    // Signature chrome: band composition rail under ribbon
    class BandCompositionRail *m_bandRail = nullptr;

    // Status bar widgets (session meta — not on band rail)
    QLabel *m_readyLabel = nullptr;
    QLabel *m_crsLabel = nullptr;
    QLabel *m_coordinatesLabel = nullptr;
    QLabel *m_scaleLabel = nullptr;
    QLabel *m_layerStatusLabel = nullptr;
    QSlider *m_statusOpacitySlider = nullptr;
    QLabel *m_statusOpacityValue = nullptr;
    QLabel *m_renderTimeLabel = nullptr;
    QLabel *m_cacheLabel = nullptr;
    QElapsedTimer m_renderTimer;

    void syncStatusBarLayer( QgsMapLayer *layer = nullptr );
    void refreshStatusTaskSummary();

    // Image Registration windows (lazy-constructed singletons)
    QgsGeoreferencerMainWindow *m_georefI2I = nullptr;
    QgsGeorefImageToMapWindow *m_georefI2M = nullptr;

    // Classification window (lazy-constructed) — Phase 10A Task 10.2
    QgsClassificationMainWindow *m_classifyWindow = nullptr;

    // OBIA window (lazy-constructed) — Phase 10B Task 10B.5
    QMainWindow *m_obiaWindow = nullptr;

    std::unique_ptr<class MapToolManager> m_toolManager;
    std::unique_ptr<sicnu::app::ProjectContext> m_projectContext;
    std::unique_ptr<ActiveViewHost> m_activeViewHost;
    std::unique_ptr<class PluginManager> m_pluginManager;

    // Lazy-loaded modules
#ifdef SICNU_EMBED_PYTHON
    std::unique_ptr<class SicnuPythonConsole> m_pythonConsole;
    class QgsDockWidget *m_pythonDock = nullptr;
    std::unique_ptr<Sicnu::PythonScriptEditor> m_pythonScriptEditor;
    class QgsDockWidget *m_pythonScriptEditorDock = nullptr;
#endif

    friend class LayerTreeMenuProvider;

protected:
    void closeEvent(QCloseEvent *event) override;
};
