/***************************************************************************
 * rs_session_map_workspace.h  —  session-local map stack (store + tree + bridge)
 *
 * Owns the layer store, layer tree, model, and (optionally) a canvas bridge
 * for a single map session (e.g. classification window). Canvas is non-owning.
 *
 * Destination CRS and extent policy are shell responsibilities; the bridge
 * is constructed with auto-setup-on-first-layer disabled so sessions do not
 * mutate QgsProject CRS / zoom from the first memory layer.
 *
 * Wave E: when registered as a ProjectContext secondary Display View, call
 * releaseLocalBridge() before createSecondaryView — DisplayManager owns the
 * only QgsLayerTreeMapCanvasBridge (dual bridges fight over canvas layers).
 ***************************************************************************/
#pragma once

#include <QObject>

#include "display/qgis_display_manager.h"

class QgsLayerTree;
class QgsLayerTreeMapCanvasBridge;
class QgsLayerTreeModel;
class QgsMapCanvas;
class QgsMapLayer;
class QgsMapLayerStore;
class QgsRectangle;

/**
 * Session-local map workspace: store + layer tree + model + canvas bridge.
 *
 * Does not own docks, QgsLayerTreeView, role pointers, CRS policy, or QMainWindow.
 * Canvas is non-owning (must outlive this object or be destroyed carefully).
 */
class RsSessionMapWorkspace : public QObject
{
    Q_OBJECT

  public:
    /**
     * Construct workspace bound to \a canvas (non-owning).
     * Owns store, root tree, model, and bridge as children of this object.
     */
    explicit RsSessionMapWorkspace( QgsMapCanvas *canvas, QObject *parent = nullptr );
    ~RsSessionMapWorkspace() override;

    /**
     * Register \a layer in the store if missing, add a tree node (unless already
     * present), and sync canvas layers. \a insertOnTop inserts at index 0;
     * otherwise appends.
     */
    void addLayer( QgsMapLayer *layer, bool insertOnTop = true );

    /**
     * Remove tree node and take the layer from the session store (takeMapLayer).
     * Does **not** delete the layer — the caller owns it afterward.
     * Re-add after remove is supported (z-order shuffle without delete).
     */
    void removeLayer( QgsMapLayer *layer );

    void setExtent( const QgsRectangle &extent );
    /**
     * Zoom canvas to \a layer extent when the layer is valid and the extent
     * is non-empty. Null-safe. CRS transform into canvas destination CRS is
     * shell / longer-term work.
     */
    void zoomToLayer( QgsMapLayer *layer );
    void setCurrentLayer( QgsMapLayer *layer );

    QgsMapCanvas *canvas() const { return m_canvas; }
    QgsLayerTree *layerTree() const { return m_layerTree; }
    QgsLayerTreeModel *layerTreeModel() const { return m_layerTreeModel; }
    QgsMapLayerStore *layerStore() const { return m_layerStore; }

    /// Host-supplied triple for ProjectContext::createSecondaryView.
    sicnu::display::DisplayViewSpec viewSpec() const;

    /// True while this object owns a session-local canvas bridge.
    bool hasLocalBridge() const { return m_layerTreeBridge != nullptr; }

    /**
     * Destroy the session-local bridge so a DisplayManager-owned bridge can
     * attach to the same tree+canvas. Safe when already released.
     */
    void releaseLocalBridge();

    /**
     * Recreate a session-local bridge after the Display View is removed.
     * No-op if a local bridge already exists or canvas is null.
     */
    void restoreLocalBridge();

  private:
    /// Push tree → canvas membership (sync when bridge present).
    void syncCanvasLayers();

    QgsMapCanvas *m_canvas = nullptr; // non-owning
    QgsMapLayerStore *m_layerStore = nullptr;
    QgsLayerTree *m_layerTree = nullptr;
    QgsLayerTreeModel *m_layerTreeModel = nullptr;
    QgsLayerTreeMapCanvasBridge *m_layerTreeBridge = nullptr;
};
