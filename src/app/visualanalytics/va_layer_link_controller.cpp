/***************************************************************************
 * va_layer_link_controller.cpp — asset-keyed layer visibility/opacity link
 ***************************************************************************/
#include "va_layer_link_controller.h"

#include <qgsmaplayer.h>
#include <qgslayertree.h>
#include <qgslayertreelayer.h>

namespace sicnu::app
{

using sicnu::display::DisplayLayerId;
using sicnu::display::DisplayViewId;

namespace
{
/// Walks a node tree collecting layer nodes (root included when it is one).
QList<QgsLayerTreeNode *> collectNodes( QgsLayerTreeNode *root )
{
    QList<QgsLayerTreeNode *> nodes;
    if ( !root )
        return nodes;
    nodes.append( root );
    if ( auto *group = QgsLayerTree::toGroup( root ) )
    {
        const QList<QgsLayerTreeNode *> children = group->children();
        for ( QgsLayerTreeNode *child : children )
            nodes.append( collectNodes( child ) );
    }
    return nodes;
}

QList<QgsLayerTreeLayer *> collectLayerNodes( QgsLayerTreeNode *root )
{
    QList<QgsLayerTreeLayer *> result;
    const QList<QgsLayerTreeNode *> nodes = collectNodes( root );
    for ( QgsLayerTreeNode *node : nodes )
    {
        if ( auto *layerNode = qobject_cast<QgsLayerTreeLayer *>( node ) )
            result.append( layerNode );
    }
    return result;
}
} // namespace

VaLayerLinkController::VaLayerLinkController( sicnu::display::QgisDisplayManager *displayManager,
                                              QObject *parent )
    : QObject( parent )
    , m_displayManager( displayManager )
{
    if ( m_displayManager )
    {
        connect( m_displayManager, &sicnu::display::QgisDisplayManager::viewAboutToBeRemoved,
                 this, &VaLayerLinkController::onViewAboutToBeRemoved );
    }
}

auto VaLayerLinkController::resolveNode( QgsLayerTreeNode *node ) const -> ResolvedLayer
{
    ResolvedLayer result;
    auto *layerNode = qobject_cast<QgsLayerTreeLayer *>( node );
    if ( !layerNode )
        return result;
    result.layer = layerNode->layer();
    return resolveLayer( result.layer );
}

auto VaLayerLinkController::resolveLayer( QgsMapLayer *layer ) const -> ResolvedLayer
{
    ResolvedLayer result;
    result.layer = layer;
    if ( !layer || !m_displayManager )
        return result;
    const std::optional<DisplayLayerId> layerId = DisplayLayerId::fromMapLayer( layer );
    if ( !layerId )
        return result;
    result.layerId = layerId->toString();
    const auto snapshot = m_displayManager->layer( *layerId );
    if ( snapshot )
        result.assetId = snapshot->assetId().toString();
    return result;
}

void VaLayerLinkController::addView( DisplayViewId viewId )
{
    if ( viewId.isNull() || !m_displayManager || !m_displayManager->view( viewId ) )
        return;
    for ( const auto &record : m_views )
        if ( record.id == viewId )
            return;
    ViewRecord record{ viewId, m_displayManager->viewLayerTree( viewId ) };
    connectTree( record );
    m_views.append( record );
}

void VaLayerLinkController::connectTree( ViewRecord &record )
{
    if ( !record.root )
        return;
    connect( record.root, &QgsLayerTreeNode::visibilityChanged, this,
             [this]( QgsLayerTreeNode *node ) { onNodeVisibilityChanged( node ); } );
    connect( record.root, &QgsLayerTreeNode::addedChildren, this,
             [this, record]( QgsLayerTreeNode *, int, int ) {
                 // Structural change: connect any nodes we have not met yet.
                 if ( record.root )
                     walkAndConnect( collectNodes( record.root ) );
             } );
    connect( record.root, &QgsLayerTreeNode::removedChildren, this,
             [this, record]( QgsLayerTreeNode *, int, int ) {
                 if ( record.root )
                     walkAndConnect( collectNodes( record.root ) );
             } );
    walkAndConnect( collectNodes( record.root ) );
}

void VaLayerLinkController::walkAndConnect( const QList<QgsLayerTreeNode *> &nodes )
{
    for ( QgsLayerTreeNode *node : nodes )
    {
        if ( !node || mConnectedNodes.contains( node ) )
            continue;
        mConnectedNodes.insert( node );
        // Prune the registry when the node dies (key-only — no deref).
        connect( node, &QObject::destroyed, this, [this, node] {
            mConnectedNodes.remove( node );
        } );
    }
    // Opacity observation is per map layer.
    for ( QgsLayerTreeNode *node : nodes )
    {
        auto *layerNode = qobject_cast<QgsLayerTreeLayer *>( node );
        QgsMapLayer *layer = layerNode ? layerNode->layer() : nullptr;
        if ( !layer || mConnectedLayers.contains( layer ) )
            continue;
        mConnectedLayers.insert( layer );
        connect( layer, &QObject::destroyed, this, [this, layer] {
            mConnectedLayers.remove( layer );
        } );
        connect( layer, &QgsMapLayer::opacityChanged, this,
                 [this]( double opacity ) { onLayerOpacityChanged( opacity ); } );
    }
}

void VaLayerLinkController::removeView( DisplayViewId viewId )
{
    m_views.removeIf( [&viewId]( const ViewRecord &record ) {
        return record.id == viewId;
    } );
}

void VaLayerLinkController::onViewAboutToBeRemoved( DisplayViewId viewId )
{
    removeView( viewId );
}

QVector<DisplayViewId> VaLayerLinkController::views() const
{
    QVector<DisplayViewId> ids;
    ids.reserve( m_views.size() );
    for ( const auto &record : m_views )
        ids.append( record.id );
    return ids;
}

void VaLayerLinkController::setVisibilitySync( bool on )
{
    mVisibilitySync = on;
}

void VaLayerLinkController::setOpacitySync( bool on )
{
    mOpacitySync = on;
    if ( !on )
        return;
    // Converge: push each asset's first-seen opacity to its peers.
    if ( !m_displayManager )
        return;
    for ( const auto &record : m_views )
    {
        if ( !record.root )
            continue;
        for ( QgsLayerTreeLayer *layerNode : collectLayerNodes( record.root ) )
        {
            const ResolvedLayer resolved = resolveNode( layerNode );
            if ( resolved.assetId.isEmpty() || !resolved.layer )
                continue;
            mApplying = true;
            syncOpacityToPeers( resolved.assetId, resolved.layer->opacity(), resolved.layer );
            mApplying = false;
        }
    }
}

void VaLayerLinkController::onNodeVisibilityChanged( QgsLayerTreeNode *node )
{
    if ( mApplying )
    {
        ++mStats.suppressedEchoes;
        return;
    }
    if ( !mVisibilitySync )
        return;
    const ResolvedLayer resolved = resolveNode( node );
    if ( resolved.assetId.isEmpty() )
    {
        // No stable identity → never guessed (DECISIONS D2).
        ++mStats.skippedUnidentifiable;
        return;
    }
    // Which view owns this node? Scan roots for the tree containing it.
    DisplayViewId sourceView;
    for ( const auto &record : m_views )
    {
        if ( !record.root )
            continue;
        for ( QgsLayerTreeLayer *layerNode : collectLayerNodes( record.root ) )
        {
            if ( layerNode == node )
            {
                sourceView = record.id;
                break;
            }
        }
        if ( !sourceView.isNull() )
            break;
    }
    if ( sourceView.isNull() )
        return;
    syncVisibilityToPeers( resolved.assetId, node->itemVisibilityChecked(), sourceView );
}

void VaLayerLinkController::syncVisibilityToPeers( const QString &assetId, bool visible,
                                                   DisplayViewId sourceView )
{
    if ( !m_displayManager )
        return;
    for ( const auto &record : m_views )
    {
        if ( record.id == sourceView || !record.root )
            continue;
        for ( QgsLayerTreeLayer *layerNode : collectLayerNodes( record.root ) )
        {
            const ResolvedLayer resolved = resolveNode( layerNode );
            if ( resolved.assetId.isEmpty() )
                continue; // not linked; do not count — only USER changes count
            if ( resolved.assetId != assetId )
                continue;
            if ( layerNode->itemVisibilityChecked() == visible )
                continue; // already coherent — not a sync
            if ( resolved.layerId.isEmpty() )
            {
                ++mStats.skippedUnidentifiable;
                continue;
            }
            const auto layerId = DisplayLayerId::fromString( resolved.layerId );
            if ( !layerId )
                continue;
            // The manager is the mutation authority; its typed result is the
            // truth about whether the peer actually moved.
            mApplying = true;
            const auto result = m_displayManager->setLayerVisible( *layerId, visible );
            mApplying = false;
            if ( !result )
                continue; // failed mutation is not a sync
            ++mStats.visibilitySyncs;
        }
    }
}

void VaLayerLinkController::onLayerOpacityChanged( double opacity )
{
    if ( mApplying )
    {
        ++mStats.suppressedEchoes;
        return;
    }
    if ( !mOpacitySync )
        return;
    QgsMapLayer *layer = qobject_cast<QgsMapLayer *>( sender() );
    if ( !layer )
        return;
    const ResolvedLayer resolved = resolveLayer( layer );
    if ( resolved.assetId.isEmpty() )
    {
        ++mStats.skippedUnidentifiable;
        return;
    }
    mApplying = true;
    syncOpacityToPeers( resolved.assetId, opacity, layer );
    mApplying = false;
}

void VaLayerLinkController::syncOpacityToPeers( const QString &assetId, double opacity,
                                                QgsMapLayer *sourceLayer )
{
    for ( const auto &record : m_views )
    {
        if ( !record.root )
            continue;
        for ( QgsLayerTreeLayer *layerNode : collectLayerNodes( record.root ) )
        {
            QgsMapLayer *layer = layerNode ? layerNode->layer() : nullptr;
            if ( !layer || layer == sourceLayer )
                continue;
            const ResolvedLayer resolved = resolveLayer( layer );
            if ( resolved.assetId.isEmpty() || resolved.assetId != assetId )
                continue;
            if ( qgsDoubleNear( layer->opacity(), opacity, 1e-6 ) )
                continue;
            layer->setOpacity( opacity );
            ++mStats.opacitySyncs;
        }
    }
}

} // namespace sicnu::app
