#pragma once

#include <memory>

#include <QObject>
#include <QList>

class QWidget;
class QgsMapCanvas;
class QgsLayerTreeView;
class QgsLayerTreeModel;
class QgsLayerTreeGroup;
class QgsLayerTreeMapCanvasBridge;
class QgsMapLayer;
class QgsVectorLayer;
class QgsMapOverviewCanvas;
class QString;

/**
 * @brief Manages all layer-related operations for the main window.
 *
 * Extracted from QgisDesktopWindow as the first step of god-class decomposition.
 * Handles layer loading (raster/vector), removal, properties display,
 * layer tree initialization, and canvas-layer synchronization.
 *
 * This is an internal implementation detail of QgisDesktopWindow;
 * the public API of the main window remains unchanged.
 */
class LayerManager : public QObject
{
    Q_OBJECT

  public:
    explicit LayerManager( QgsMapCanvas *canvas,
                           QgsLayerTreeView *treeView,
                           QgsMapOverviewCanvas *overviewCanvas,
                           QWidget *parentWidget );

    ~LayerManager() override;

    // ── Layer tree initialization ─────────────────────────────────────
    void initLayerTree();

    // ── Core layer loading (programmatic, no dialogs) ─────────────────
    void loadRasterLayer( const QString &filePath );
    void loadVectorLayer( const QString &filePath );

    // ── Layer operations ──────────────────────────────────────────────
    void showLayerProperties( QgsMapLayer *layer );
    void removeSelectedLayers();
    void refreshCanvasLayers();

    // ── Layer queries ─────────────────────────────────────────────────
    QgsMapLayer *activeLayer();
    QList<QgsMapLayer *> selectedLayers();

    // ── Accessors ─────────────────────────────────────────────────────
    QgsLayerTreeModel *layerTreeModel() const { return m_layerTreeModel; }

    // ── Layer tree helpers ────────────────────────────────────────────
    QgsLayerTreeGroup *findOrCreateGroup( const QString &name );

  private:
    QgsMapCanvas *m_mapCanvas = nullptr;
    QgsLayerTreeView *m_layerTreeView = nullptr;
    QgsMapOverviewCanvas *m_overviewCanvas = nullptr;
    QWidget *m_parentWidget = nullptr;

    QgsLayerTreeModel *m_layerTreeModel = nullptr;
    QgsLayerTreeMapCanvasBridge *m_layerTreeBridge = nullptr;
};
