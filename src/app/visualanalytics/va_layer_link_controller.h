/***************************************************************************
 * va_layer_link_controller.h — Linked Visual Analytics 11.0 layer link
 *
 * Syncs layer VISIBILITY (and opacity where the platform exposes it)
 * across display views keyed by the stable cross-view identity: the
 * catalog AssetId (DECISIONS D2). Layers without an asset id are never
 * linked — no name guessing, fail-closed honesty.
 *
 * Observation rides the layer-tree authority the display manager itself
 * writes through (QgisDisplayManager::setLayerVisible →
 * QgsLayerTreeNode::setItemVisibilityChecked → visibilityChanged), so no
 * second visibility state and no display-manager change is needed; all
 * peer mutations go back through the manager (or QgsMapLayer::setOpacity
 * for opacity, which has no manager-level API). An mApplying guard makes
 * the resulting echo signals inert — no feedback loop.
 *
 * Lifecycle: per-view root-tree connections auto-disconnect when either
 * endpoint dies; tracked node/layer sets use QPointer and are pruned on
 * tree structural changes; viewAboutToBeRemoved detaches the view.
 ***************************************************************************/
#pragma once

#include <QObject>
#include <QPointer>
#include <QSet>
#include <QVector>

#include "display/qgis_display_manager.h"

class QgsLayerTreeNode;
class QgsMapLayer;

namespace sicnu::app
{

class VaLayerLinkController : public QObject
{
    Q_OBJECT
  public:
    struct Stats
    {
        quint64 visibilitySyncs = 0;  ///< peer visibility applications
        quint64 opacitySyncs = 0;     ///< peer opacity applications
        /// Layer changes that could not be resolved to an asset id and were
        /// therefore never linked (honest, counted — never guessed).
        quint64 skippedUnidentifiable = 0;
        /// Echo signals absorbed by the applying guard.
        quint64 suppressedEchoes = 0;
    };

    explicit VaLayerLinkController( sicnu::display::QgisDisplayManager *displayManager,
                                    QObject *parent = nullptr );

    void addView( sicnu::display::DisplayViewId viewId );
    void removeView( sicnu::display::DisplayViewId viewId );
    QVector<sicnu::display::DisplayViewId> views() const;

    bool visibilitySyncEnabled() const { return mVisibilitySync; }
    bool opacitySyncEnabled() const { return mOpacitySync; }

  public slots:
    void setVisibilitySync( bool on );
    /// Applies the current opacity of every asset's first layer to its peers.
    void setOpacitySync( bool on );

    /// Testing instrumentation.
    Stats stats() const { return mStats; }
    void resetStats() { mStats = Stats{}; }

  private slots:
    void onViewAboutToBeRemoved( sicnu::display::DisplayViewId viewId );

  private:
    struct ViewRecord
    {
        sicnu::display::DisplayViewId id;
        QPointer<QgsLayerTreeNode> root;
    };

    void connectTree( ViewRecord &record );
    void walkAndConnect( const QList<QgsLayerTreeNode *> &nodes );
    /// node visibility changed → resolve asset → apply to peers (the node's
    /// view is identified by scanning records for the owning root).
    void onNodeVisibilityChanged( QgsLayerTreeNode *node );
    void onLayerOpacityChanged( double opacity );
    /// Applies @p visible to every view whose tree holds the same asset
    /// (skipping the source). Authority: QgisDisplayManager::setLayerVisible.
    void syncVisibilityToPeers( const QString &assetId, bool visible,
                                sicnu::display::DisplayViewId sourceView );
    void syncOpacityToPeers( const QString &assetId, double opacity,
                             QgsMapLayer *sourceLayer );

    /// Resolves a tree node to (layer, DisplayLayerId, AssetId). Empty
    /// assetId = unidentifiable.
    struct ResolvedLayer
    {
        QgsMapLayer *layer = nullptr;
        QString layerId;
        QString assetId;
    };
    ResolvedLayer resolveNode( QgsLayerTreeNode *node ) const;
    ResolvedLayer resolveLayer( QgsMapLayer *layer ) const;

    sicnu::display::QgisDisplayManager *m_displayManager = nullptr;
    QVector<ViewRecord> m_views;
    bool mVisibilitySync = true;
    bool mOpacitySync = true;
    bool mApplying = false;
    /// Nodes/layers already connected. Entries are removed by each object's
    /// destroyed() signal (key-only removal — never dereferenced there), so
    /// the sets never hold stale addresses that could suppress a connect.
    QSet<QgsLayerTreeNode *> mConnectedNodes;
    QSet<QgsMapLayer *> mConnectedLayers;
    Stats mStats;
};

} // namespace sicnu::app
