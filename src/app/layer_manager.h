#pragma once

#include <memory>

#include <QObject>
#include <QList>

#include "display/qgis_display_manager.h"

class QWidget;
class QgsMapCanvas;
class QgsLayerTreeView;
class QgsLayerTreeModel;
class QgsLayerTreeGroup;
class QgsMapLayer;
class QgsVectorLayer;
class QgsMapOverviewCanvas;
class QString;

namespace sicnu::data {
class DataManager;
struct SourceDescriptor;
}

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
                           sicnu::data::DataManager *dataManager,
                           sicnu::display::QgisDisplayManager *displayManager,
                           sicnu::display::DisplayViewId mainViewId,
                           QWidget *parentWidget );

    ~LayerManager() override;

    // ── Layer tree initialization ─────────────────────────────────────
    void initLayerTree();

    // ── Core layer loading (programmatic, no dialogs) ─────────────────
    sicnu::data::Result<sicnu::display::DisplayLayerId>
    loadLayer( const QString &filePath );
    sicnu::data::Result<sicnu::display::DisplayLayerId>
    loadRasterLayer( const QString &filePath );
    sicnu::data::Result<sicnu::display::DisplayLayerId>
    loadVectorLayer( const QString &filePath );

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
    sicnu::data::DataManager *m_dataManager = nullptr;
    sicnu::display::QgisDisplayManager *m_displayManager = nullptr;
    sicnu::display::DisplayViewId m_mainViewId;
    QWidget *m_parentWidget = nullptr;

    QgsLayerTreeModel *m_layerTreeModel = nullptr;

    sicnu::data::Result<sicnu::display::DisplayLayerId>
    loadSource( sicnu::data::SourceDescriptor source );
    void reportDiagnostics( const QString &title,
                            const QVector<sicnu::data::Diagnostic> &diagnostics );
};
