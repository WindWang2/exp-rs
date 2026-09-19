// src/app/pipeline/pipeline_scene.cpp — snapping + wiring interaction (D17)
#include "pipeline_scene.h"

#include <QGraphicsSceneMouseEvent>
#include <algorithm>
#include <cmath>

namespace sicnu::app::pipeline {
namespace {

QPointF outputPortPosByName( PipelineNodeItem *node, const QString &portName )
{
    if ( !node )
        return {};
    for ( int i = 0; i < node->outputPortCount(); ++i )
    {
        if ( node->outputPortName( i ) == portName )
            return node->outputPortScenePos( i );
    }
    return node->outputPortScenePos( 0 );
}

QPointF inputPortPosByName( PipelineNodeItem *node, const QString &portName )
{
    if ( !node )
        return {};
    for ( int i = 0; i < node->inputPortCount(); ++i )
    {
        if ( node->inputPortName( i ) == portName )
            return node->inputPortScenePos( i );
    }
    return node->inputPortScenePos( 0 );
}

/// Keep a wire glued to named ports as endpoints move (#1085).
void attachConnectionMoveTracking( PipelineConnectionItem *connection,
                                   PipelineNodeItem *source, PipelineNodeItem *target,
                                   const QString &sourcePort, const QString &targetPort )
{
    if ( !connection || !source || !target )
        return;
    QPointer<PipelineConnectionItem> connectionRef( connection );
    QPointer<PipelineNodeItem> sourceRef( source );
    QPointer<PipelineNodeItem> targetRef( target );
    const QString srcPort = sourcePort;
    const QString dstPort = targetPort;
    QObject::connect( source, &PipelineNodeItem::nodeMoved, connection,
                      [connectionRef, sourceRef, srcPort]() {
                          if ( connectionRef && sourceRef )
                              connectionRef->updateEndpoints(
                                  outputPortPosByName( sourceRef.data(), srcPort ),
                                  connectionRef->path().pointAtPercent( 1 ) );
                      } );
    QObject::connect( target, &PipelineNodeItem::nodeMoved, connection,
                      [connectionRef, targetRef, dstPort]() {
                          if ( connectionRef && targetRef )
                              connectionRef->updateEndpoints(
                                  connectionRef->path().pointAtPercent( 0 ),
                                  inputPortPosByName( targetRef.data(), dstPort ) );
                      } );
}

} // namespace

PipelineScene::PipelineScene( QObject *parent )
    : QGraphicsScene( parent )
{
    setItemIndexMethod( QGraphicsScene::NoIndex ); // stable for 100-node churn
    setSceneRect( 0, 0, 4000, 3000 );
}

void PipelineScene::mousePressEvent( QGraphicsSceneMouseEvent *event )
{
    if ( event->button() == Qt::LeftButton )
    {
        // Start a wire from an output port under the cursor.
        const QPointF pos = event->scenePos();
        for ( auto it = m_nodes.cbegin(); it != m_nodes.cend(); ++it )
        {
            PipelineNodeItem *node = it.value().data();
            if ( !node )
                continue;
            for ( int i = 0; i < node->outputPortCount(); ++i )
            {
                if ( QLineF( node->outputPortScenePos( i ), pos ).length() <= kSnapRadiusPx )
                {
                    // The wire references the port's REAL name — a literal
                    // guess would produce a semantically invalid document.
                    beginPendingConnection( node->nodeId(), node->outputPortName( i ),
                                            node->outputPortScenePos( i ) );
                    event->accept();
                    return;
                }
            }
        }
        // Selection reporting for the property panel.
        const QList<QGraphicsItem *> picked = items( pos, Qt::IntersectsItemShape, Qt::DescendingOrder );
        for ( QGraphicsItem *item : picked )
            if ( auto *node = qgraphicsitem_cast<PipelineNodeItem *>( item ) )
            {
                emit nodeSelected( node->nodeId() );
                break;
            }
    }
    QGraphicsScene::mousePressEvent( event );
}

void PipelineScene::mouseMoveEvent( QGraphicsSceneMouseEvent *event )
{
    if ( m_pendingConnection )
        updatePendingConnection( event->scenePos() );
    QGraphicsScene::mouseMoveEvent( event );
}

void PipelineScene::mouseReleaseEvent( QGraphicsSceneMouseEvent *event )
{
    if ( m_pendingConnection && event->button() == Qt::LeftButton )
    {
        PipelinePortItem *target = portAtScenePos( event->scenePos() );
        if ( !finishPendingConnection( target ) )
            cancelPendingConnection();
        event->accept();
        return;
    }
    QGraphicsScene::mouseReleaseEvent( event );
}

void PipelineScene::addNodeItem( PipelineNodeItem *node )
{
    if ( !node )
        return;
    addItem( node );
    m_nodes.insert( node->nodeId(), node );
}

bool PipelineScene::withinSnapRadius( const QPointF &a, const QPointF &b, qreal radius )
{
    return std::sqrt( std::pow( a.x() - b.x(), 2 ) + std::pow( a.y() - b.y(), 2 ) ) <= radius;
}

PipelinePortItem *PipelineScene::portAtScenePos( const QPointF &scenePos, qreal maxDistance ) const
{
    PipelinePortItem *best = nullptr;
    qreal bestDistance = maxDistance;
    for ( auto it = m_nodes.cbegin(); it != m_nodes.cend(); ++it )
    {
        PipelineNodeItem *node = it.value().data();
        if ( !node )
            continue;
        for ( QGraphicsItem *child : node->childItems() )
        {
            auto *port = qgraphicsitem_cast<PipelinePortItem *>( child );
            if ( !port || port->direction != PortDirection::Input )
                continue;
            const qreal distance = QLineF( port->sceneCenter(), scenePos ).length();
            if ( distance <= bestDistance )
            {
                bestDistance = distance;
                best = port;
            }
        }
    }
    return best;
}

void PipelineScene::beginPendingConnection( const QString &sourceNodeId, const QString &sourcePort,
                                            const QPointF &start )
{
    cancelPendingConnection();
    m_pendingSourceNode = sourceNodeId;
    m_pendingSourcePort = sourcePort;
    m_pendingConnection = new PipelineConnectionItem( start, start );
    addItem( m_pendingConnection );
}

void PipelineScene::updatePendingConnection( const QPointF &scenePos )
{
    if ( m_pendingConnection )
        m_pendingConnection->updateEndpoints( m_pendingConnection->path().pointAtPercent( 0 ), scenePos );
}

bool PipelineScene::finishPendingConnection( PipelinePortItem *target )
{
    if ( !m_pendingConnection || !target )
        return false;

    const QPointF end = target->sceneCenter();
    m_pendingConnection->updateEndpoints( m_pendingConnection->path().pointAtPercent( 0 ), end );
    m_pendingConnection->sourceNodeId = m_pendingSourceNode;
    m_pendingConnection->sourcePortName = m_pendingSourcePort;
    m_pendingConnection->targetNodeId = target->ownerNode ? target->ownerNode->nodeId() : QString();
    m_pendingConnection->targetPortName = target->portName;

    const QString source = m_pendingSourceNode;
    const QString sourcePort = m_pendingSourcePort;
    const QString targetNode = m_pendingConnection->targetNodeId;
    const QString targetPort = target->portName;

    // #1085: interactive wires used to skip move-tracking, so dragging either
    // endpoint left the wire frozen at drop-time geometry.
    PipelineNodeItem *sourceItem = m_nodes.value( source ).data();
    PipelineNodeItem *targetItem = target->ownerNode;
    attachConnectionMoveTracking( m_pendingConnection.data(), sourceItem, targetItem,
                                  sourcePort, targetPort );

    m_pendingConnection.clear(); // ownership stays with the scene

    emit connectionCreated( source, sourcePort, targetNode, targetPort );
    return true;
}

void PipelineScene::cancelPendingConnection()
{
    if ( m_pendingConnection )
    {
        removeItem( m_pendingConnection );
        delete m_pendingConnection;
        m_pendingConnection.clear();
    }
}

PipelineConnectionItem *PipelineScene::addConnection( const QString &sourceNodeId, const QString &sourcePort,
                                                      const QString &targetNodeId, const QString &targetPort )
{
    PipelineNodeItem *source = m_nodes.value( sourceNodeId ).data();
    PipelineNodeItem *target = m_nodes.value( targetNodeId ).data();
    if ( !source || !target )
        return nullptr;

    // Anchor on the named ports when present; otherwise fall back to index 0.
    const QPointF start = outputPortPosByName( source, sourcePort );
    const QPointF end = inputPortPosByName( target, targetPort );

    auto *connection = new PipelineConnectionItem( start, end );
    connection->sourceNodeId = sourceNodeId;
    connection->sourcePortName = sourcePort;
    connection->targetNodeId = targetNodeId;
    connection->targetPortName = targetPort;
    addItem( connection );

    // #1085: track by port *name*, not index 0 — multi-port nodes otherwise
    // jump their wires to the first port on the first drag after load.
    attachConnectionMoveTracking( connection, source, target, sourcePort, targetPort );
    return connection;
}

} // namespace sicnu::app::pipeline
