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
class LayerManager;
class QPainter;
class QTextBrowser;
class LayerTreeMenuProvider;
class QgsBrowserDockWidget;
class QgsBrowserGuiModel;
class SpectralProfileWidget;
class QgsGeoreferencerMainWindow;
class QgsClassificationMainWindow;
class QgsAdvancedDigitizingDockWidget;
class QgsMessageBar;
class QgsMapToolSelect;
class QgsMapToolAddFeature;
class QgsMapToolMoveFeature;
class QgsMapToolRotateFeature;
class QgsMapToolScaleFeature;
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

    QgsMapCanvas *mapCanvas() const { return m_mapCanvas; }
    QgsMapLayer *activeLayer();
    QList<QgsMapLayer*> selectedLayers();

public slots:
    void addRasterLayer();
    void addVectorLayer();
    void newProject();
    void openProject();
    void saveProject();
    void saveProjectAs();
    void importLayer();
    void browseStacCatalog();
    void newLayout();
    void undo();
    void redo();
    void cutFeatures();
    void copyFeatures();
    void pasteFeatures();
    void selectAll();
    void zoomIn();
    void zoomOut();
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

    // Enhancement dialogs
    void openContrastStretchDialog();
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

    // Georeferencer (Task 11.4.4)
    void openGeoreferencer();

    // Classification (Phase 10A Task 10.2)
    void openClassificationWindow();

    // OBIA Classification (Phase 10B Task 10B.5)
    void openObiaWindow();

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
    void zoomFullExtent();
    void zoomToLayer();

private:
    void setupMenu();
    void setupToolbars();
    void setupDockWidgets();
    void setupStatusBar();
    void setupConnections();

    QgsLayerTreeGroup *findOrCreateGroup(const QString &name);
    QgsVectorLayer *currentVectorLayer();
    void updateEditingUI(QgsVectorLayer *vlayer);
    void loadRasterLayer(const QString &filePath);
    void loadVectorLayer(const QString &filePath);
    void showLayerProperties(QgsMapLayer *layer);
    void refreshCanvasLayers();
    void openProcessingAlgorithm(const QString &algorithmId);

    // Panel state persistence
    void savePanelState();
    void restorePanelState();
    void resetPanelLayout();

    bool confirmSaveEdits(QgsVectorLayer *vl);
    bool checkUnsavedChanges();
    void applyDarkPalette();

    // QGIS C++ components
    QgsMapCanvas *m_mapCanvas = nullptr;
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
    CustomIdentifyTool *m_identifyTool = nullptr;
    MeasureTool *m_measureDistanceTool = nullptr;
    MeasureTool *m_measureAreaTool = nullptr;

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
    QgsDockWidget *m_logDock = nullptr;
    QgsDockWidget *m_workflowDock = nullptr;
    QMenu *m_windowMenu = nullptr;

    // Identify results display
    QTextBrowser *m_identifyResults = nullptr;

    // Spectral profile display
    SpectralProfileWidget *m_spectralProfile = nullptr;

    // Status bar widgets
    QLabel *m_readyLabel = nullptr;
    QLabel *m_crsLabel = nullptr;
    QLabel *m_coordinatesLabel = nullptr;
    QLabel *m_scaleLabel = nullptr;
    QLabel *m_renderTimeLabel = nullptr;
    QLabel *m_cacheLabel = nullptr;
    QElapsedTimer m_renderTimer;

    // Georeferencer window (lazy-constructed) — Task 11.4.4
    QgsGeoreferencerMainWindow *m_georefWindow = nullptr;

    // Classification window (lazy-constructed) — Phase 10A Task 10.2
    QgsClassificationMainWindow *m_classifyWindow = nullptr;

    // OBIA window (lazy-constructed) — Phase 10B Task 10B.5
    QMainWindow *m_obiaWindow = nullptr;

    std::unique_ptr<class MapToolManager> m_toolManager;
    std::unique_ptr<LayerManager> m_layerManager;
    std::unique_ptr<class PluginManager> m_pluginManager;

    // Lazy-loaded modules
    std::unique_ptr<class SicnuPythonConsole> m_pythonConsole;
    class QgsDockWidget *m_pythonDock = nullptr;

    friend class LayerTreeMenuProvider;

protected:
    void closeEvent(QCloseEvent *event) override;
};
