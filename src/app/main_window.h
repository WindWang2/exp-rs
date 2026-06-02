#pragma once

#include <QMainWindow>
#include <QLabel>
#include <QHBoxLayout>
#include <QCloseEvent>
#include <QSettings>

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
#include <qgslayertreemapcanvasbridge.h>

#include "map_tools/measure_tool.h"

class QDomDocument;
class QPainter;
class QTextBrowser;
class LayerTreeMenuProvider;
class QgsBrowserDockWidget;
class QgsBrowserGuiModel;
class SpectralProfileWidget;
class QgsGeoreferencerMainWindow;

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

    void setupUi();
    void setupMapCanvas();
    void initLayerTree();

    QgsMapCanvas *mapCanvas() const { return m_mapCanvas; }

public slots:
    void addRasterLayer();
    void addVectorLayer();
    void newProject();
    void openProject();
    void saveProject();
    void saveProjectAs();
    void importLayer();
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
    void options();
    void showProcessingToolbox();
    void showProcessingHistory();

    // Raster processing dialogs
    void openBandMathDialog();
    void openSpectralIndexDialog();
    void openAtmosphericCorrectionDialog();

    // Enhancement dialogs
    void openContrastStretchDialog();
    void openSpatialFilterDialog();
    void openPcaDialog();
    void openBandRatioDialog();

    // CRS preset dialog
    void openCrsPresetDialog();
    void setLayerCrsFromPreset();

    // Measurement tools
    void measureDistance();
    void measureArea();

    // Georeferencer (Task 11.4.4)
    void openGeoreferencer();

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
    void loadRasterLayer(const QString &filePath);
    void loadVectorLayer(const QString &filePath);
    void showLayerProperties(QgsMapLayer *layer);
    void refreshCanvasLayers();
    QList<QgsMapLayer*> selectedLayers();
    void openProcessingAlgorithm(const QString &algorithmId);

    // Panel state persistence
    void savePanelState();
    void restorePanelState();
    void resetPanelLayout();

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

    // Dock widgets
    QgsDockWidget *m_layersDock = nullptr;
    QgsBrowserDockWidget *m_browserDock = nullptr;
    QgsBrowserGuiModel *m_browserModel = nullptr;
    QgsDockWidget *m_processingDock = nullptr;
    QgsDockWidget *m_overviewDock = nullptr;
    QgsMapOverviewCanvas *m_overviewCanvas = nullptr;
    QgsLayerTreeMapCanvasBridge *m_layerTreeBridge = nullptr;
    QgsDockWidget *m_identifyDock = nullptr;
    QgsDockWidget *m_spectralDock = nullptr;
    QgsDockWidget *m_logDock = nullptr;
    QMenu *m_windowMenu = nullptr;

    // Identify results display
    QTextBrowser *m_identifyResults = nullptr;

    // Spectral profile display
    SpectralProfileWidget *m_spectralProfile = nullptr;

    // Status bar widgets
    QLabel *m_crsLabel = nullptr;
    QLabel *m_coordinatesLabel = nullptr;
    QLabel *m_scaleLabel = nullptr;
    QLabel *m_renderTimeLabel = nullptr;
    QLabel *m_cacheLabel = nullptr;

    // Georeferencer window (lazy-constructed) — Task 11.4.4
    QgsGeoreferencerMainWindow *m_georefWindow = nullptr;

    friend class LayerTreeMenuProvider;

protected:
    void closeEvent(QCloseEvent *event) override;
};
