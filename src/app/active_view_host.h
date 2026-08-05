#pragma once

#include <memory>

#include <QObject>
#include <QList>

#include "qgis.h"
#include <qgsmapcanvas.h>
#include <qgsrectangle.h>
#include "data/asset_types.h"
#include "display/qgis_display_manager.h"

class QWidget;
class QgsMessageBar;
class QgsLayerTreeView;
class QgsLayerTreeModel;
class QgsLayerTreeGroup;
class QgsMapLayer;
class QgsVectorLayer;
class QgsMapOverviewCanvas;
class QgsRectangle;
class QString;

namespace sicnu::data {
class DataManager;
struct SourceDescriptor;
}

struct ViewportSnapshot
{
    QgsRectangle extent;
    double scale = 1.0;
    QString crsAuthId;
    QString activeLayerName;
};

/**
 * ActiveViewHost — shell façade for the **active Display View**.
 *
 * **Not** the project data authority (`sicnu::data::DataManager`, ADR 0009/0010).
 * This module only:
 *   - opens a path by registering a Data Asset and displaying it on the active view,
 *   - displays an existing Asset on the active view,
 *   - removes selected Display Layers (does not unload Assets),
 *   - drives the active view’s layer-tree selection, properties, and canvas refresh.
 *
 * Multi-view hosts pass a non-main `activeViewId` once secondary views exist.
 * Catalog / unload / promote stay on DataManager (+ Data Manager panel).
 *
 * Internal to QgisDesktopWindow; main-window public load* slots remain stable.
 */
class ActiveViewHost : public QObject
{
    Q_OBJECT

  public:
    explicit ActiveViewHost( QgsMapCanvas *canvas,
                             QgsLayerTreeView *treeView,
                             QgsMapOverviewCanvas *overviewCanvas,
                             sicnu::data::DataManager *dataManager,
                             sicnu::display::QgisDisplayManager *displayManager,
                             sicnu::display::DisplayViewId mainViewId,
                             QWidget *parentWidget );

    ~ActiveViewHost() override;

    // ── Active view ───────────────────────────────────────────────────
    sicnu::display::DisplayViewId mainViewId() const { return m_mainViewId; }
    /// The active view id is owned by the display manager (single source of
    /// truth, ADR 0019); this host only forwards to it.
    sicnu::display::DisplayViewId activeViewId() const
    {
        return m_displayManager ? m_displayManager->activeViewId()
                                : sicnu::display::DisplayViewId();
    }
    QgsLayerTreeView *layerTreeView() const { return m_layerTreeView; }
    QgsMapCanvas *mapCanvas() const { return m_mapCanvas; }
    void setMessageBar( QgsMessageBar *messageBar ) { m_messageBar = messageBar; }
    QgsMessageBar *messageBar() const { return m_messageBar; }
    sicnu::display::QgisDisplayManager *displayManager() const { return m_displayManager; }
    QgsMapLayer *mapLayer( sicnu::display::DisplayLayerId displayLayerId ) const
    {
        return m_displayManager ? m_displayManager->mapLayer( displayLayerId ) : nullptr;
    }

    /// Target view for open/display. Must be a live view known to DisplayManager.
    /// Defaults to mainViewId. Returns false if id is null / unknown.
    bool setActiveViewId( sicnu::display::DisplayViewId viewId );

    // ── Shell facade methods ──────────────────────────────────────────
    void pushMessageBarAlert( const QString &title, const QString &text, Qgis::MessageLevel level = Qgis::MessageLevel::Info );
    QgsRectangle mapCanvasExtent() const { return m_mapCanvas ? m_mapCanvas->extent() : QgsRectangle(); }
    double mapCanvasScale() const { return m_mapCanvas ? m_mapCanvas->scale() : 1.0; }
    QString mapCanvasCrsAuthId() const { return m_mapCanvas ? m_mapCanvas->mapSettings().destinationCrs().authid() : QString(); }
    ViewportSnapshot viewportSnapshot() const
    {
        if ( !m_mapCanvas )
            return ViewportSnapshot{};
        ViewportSnapshot snap;
        snap.extent = m_mapCanvas->extent();
        snap.scale = m_mapCanvas->scale();
        snap.crsAuthId = m_mapCanvas->mapSettings().destinationCrs().authid();
        snap.activeLayerName = activeLayerName();
        return snap;
    }

    void setExtent( const QgsRectangle &extent );
    void setCenter( const QgsPointXY &center );
    void setScale( double scale );
    void zoomToFullExtent();
    void refreshCanvas();

    // ── Layer tree (active / main QGIS tree for main view) ────────────
    void initLayerTree();
    QgsLayerTreeModel *layerTreeModel() const { return m_layerTreeModel; }
    QgsLayerTreeGroup *findOrCreateGroup( const QString &name );

    // ── Open path → register Asset + display on active view ───────────
    sicnu::data::Result<sicnu::display::DisplayLayerId>
    openPath( const QString &filePath );
    sicnu::data::Result<sicnu::display::DisplayLayerId>
    openRasterPath( const QString &filePath );
    sicnu::data::Result<sicnu::display::DisplayLayerId>
    openVectorPath( const QString &filePath );

    /// Display an already-registered Asset on the active view (no re-register).
    sicnu::data::Result<sicnu::display::DisplayLayerId>
    displayAsset( sicnu::data::AssetId assetId );

    // Compatibility names used by main window during migration
    sicnu::data::Result<sicnu::display::DisplayLayerId>
    loadLayer( const QString &filePath ) { return openPath( filePath ); }
    sicnu::data::Result<sicnu::display::DisplayLayerId>
    loadRasterLayer( const QString &filePath ) { return openRasterPath( filePath ); }
    sicnu::data::Result<sicnu::display::DisplayLayerId>
    loadVectorLayer( const QString &filePath ) { return openVectorPath( filePath ); }

    // ── Display-layer operations (not Asset unload) ───────────────────
    void showLayerProperties( QgsMapLayer *layer );
    void removeSelectedDisplayLayers();
    void removeSelectedLayers() { removeSelectedDisplayLayers(); }
    void refreshCanvasLayers();
    void zoomToLayer( QgsMapLayer *layer = nullptr );
    void zoomToNativeResolution( QgsMapLayer *layer = nullptr );

    /// Prefer the map canvas current layer; fall back to the first layer-tree selection.
    QgsMapLayer *activeLayer() const;
    /// Inline-safe name query for agent snapshot capture (no app .cpp link required).
    QString activeLayerName() const
    {
      if ( m_mapCanvas && m_mapCanvas->currentLayer() )
        return m_mapCanvas->currentLayer()->name();
      return QString();
    }
    QList<QgsMapLayer *> selectedLayers() const;

  private:
    QgsMapCanvas *m_mapCanvas = nullptr;
    QgsLayerTreeView *m_layerTreeView = nullptr;
    QgsMapOverviewCanvas *m_overviewCanvas = nullptr;
    QgsMessageBar *m_messageBar = nullptr;
    sicnu::data::DataManager *m_dataManager = nullptr;
    sicnu::display::QgisDisplayManager *m_displayManager = nullptr;
    sicnu::display::DisplayViewId m_mainViewId;
    QWidget *m_parentWidget = nullptr;

    QgsLayerTreeModel *m_layerTreeModel = nullptr;

    sicnu::data::Result<sicnu::display::DisplayLayerId>
    openSource( sicnu::data::SourceDescriptor source );
    void reportDiagnostics( const QString &title,
                            const QVector<sicnu::data::Diagnostic> &diagnostics );
    void placeInTreeGroup( QgsMapLayer *layer, sicnu::data::AssetKind kind );
};
