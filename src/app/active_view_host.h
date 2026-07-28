#pragma once

#include <memory>

#include <QObject>
#include <QList>

#include "qgis.h"
#include "data/asset_types.h"
#include "display/qgis_display_manager.h"

class QWidget;
class QgsMapCanvas;
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
    sicnu::display::DisplayViewId activeViewId() const { return m_activeViewId; }
    QgsLayerTreeView *layerTreeView() const { return m_layerTreeView; }
    QgsMapCanvas *mapCanvas() const { return m_mapCanvas; }
    void setMessageBar( QgsMessageBar *messageBar ) { m_messageBar = messageBar; }
    QgsMessageBar *messageBar() const { return m_messageBar; }

    /// Target view for open/display. Must be a live view known to DisplayManager.
    /// Defaults to mainViewId. Returns false if id is null / unknown.
    bool setActiveViewId( sicnu::display::DisplayViewId viewId );

    // ── Shell facade methods ──────────────────────────────────────────
    void pushMessageBarAlert( const QString &title, const QString &text, Qgis::MessageLevel level = Qgis::MessageLevel::Info );
    QgsRectangle mapCanvasExtent() const;
    double mapCanvasScale() const;

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

    QgsMapLayer *activeLayer();
    QList<QgsMapLayer *> selectedLayers();

  private:
    QgsMapCanvas *m_mapCanvas = nullptr;
    QgsLayerTreeView *m_layerTreeView = nullptr;
    QgsMapOverviewCanvas *m_overviewCanvas = nullptr;
    QgsMessageBar *m_messageBar = nullptr;
    sicnu::data::DataManager *m_dataManager = nullptr;
    sicnu::display::QgisDisplayManager *m_displayManager = nullptr;
    sicnu::display::DisplayViewId m_mainViewId;
    sicnu::display::DisplayViewId m_activeViewId;
    QWidget *m_parentWidget = nullptr;

    QgsLayerTreeModel *m_layerTreeModel = nullptr;

    sicnu::data::Result<sicnu::display::DisplayLayerId>
    openSource( sicnu::data::SourceDescriptor source );
    void reportDiagnostics( const QString &title,
                            const QVector<sicnu::data::Diagnostic> &diagnostics );
    void placeInTreeGroup( QgsMapLayer *layer, sicnu::data::AssetKind kind );
};
