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
 * Active-view display host for the main window (legacy name: LayerManager).
 *
 * **Not** the project data authority — that is `sicnu::data::DataManager`
 * (ADR 0009 / 0010). This façade only:
 *   - registers a local path as a Data Asset (when DataManager is wired), and
 *   - adds a Display Layer to the **active / main** Display View, and
 *   - drives the main view’s layer tree selection, properties, and canvas refresh.
 *
 * Planned rename: ActiveViewHost. Callers that need catalog identity must use
 * DataManager; callers that need multi-view must use DisplayManager + view id.
 *
 * Internal to QgisDesktopWindow; main-window public API stays stable.
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

    // ── Layer tree initialization (main Display View) ─────────────────
    void initLayerTree();

    // ── Open path → register Asset + display on main/active view ──────
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
