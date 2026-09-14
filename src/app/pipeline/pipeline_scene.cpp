// src/app/pipeline/pipeline_scene.cpp — snapping + wiring interaction (D17)
#include "pipeline_scene.h"

#include <QGraphicsSceneMouseEvent>
#include <algorithm>
#include <cmath>

namespace sicnu::app::pipeline {

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
                    beginPendingConnection( node->nodeId(), QStringLiteral( "output" ), node->outputPortScenePos( i ) );
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
    m_connections.append( m_pendingConnection );

    const QString source = m_pendingSourceNode;
    const QString sourcePort = m_pendingSourcePort;
    const QString targetNode = m_pendingConnection->targetNodeId;
    const QString targetPort = target->portName;
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

    // Anchor on the ports when present; otherwise the node edges.
    QPointF start = source->outputPortScenePos( 0 );
    QPointF end = target->inputPortScenePos( 0 );
    for ( QGraphicsItem *child : source->childItems() )
        if ( auto *port = qgraphicsitem_cast<PipelinePortItem *>( child ) )
            if ( port->direction == PortDirection::Output && port->portName == sourcePort )
                start = port->sceneCenter();
    for ( QGraphicsItem *child : target->childItems() )
        if ( auto *port = qgraphicsitem_cast<PipelinePortItem *>( child ) )
            if ( port->direction == PortDirection::Input && port->portName == targetPort )
                end = port->sceneCenter();

    auto *connection = new PipelineConnectionItem( start, end );
    connection->sourceNodeId = sourceNodeId;
    connection->sourcePortName = sourcePort;
    connection->targetNodeId = targetNodeId;
    connection->targetPortName = targetPort;
    addItem( connection );
    m_connections.append( connection );

    // Keep the wire glued to both endpoints as nodes move.
    QPointer<PipelineConnectionItem> connectionRef( connection );
    QPointer<PipelineNodeItem> sourceRef( source );
    QPointer<PipelineNodeItem> targetRef( target );
    QObject::connect( source, &PipelineNodeItem::nodeMoved, connection, [connectionRef, sourceRef]() {
        if ( connectionRef && sourceRef )
            connectionRef->updateEndpoints( sourceRef->outputPortScenePos( 0 ),
                                            connectionRef->path().pointAtPercent( 1 ) );
    } );
    QObject::connect( target, &PipelineNodeItem::nodeMoved, connection, [connectionRef, targetRef]() {
        if ( connectionRef && targetRef )
            connectionRef->updateEndpoints( connectionRef->path().pointAtPercent( 0 ),
                                            targetRef->inputPortScenePos( 0 ) );
    } );
    return connection;
}

} // namespace sicnu::app::pipeline
