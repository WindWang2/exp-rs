// src/app/pipeline/pipeline_canvas_widget.cpp — document ⇄ items projection (D17)
#include "pipeline_canvas_widget.h"

#include "pipeline_connection_item.h"
#include "pipeline_node_item.h"
#include "pipeline_port_item.h"
#include "pipeline_scene.h"

#include <algorithm>
#include <QSet>

namespace sicnu::app::pipeline {
namespace {
constexpr qreal kMinZoom = 0.2;
constexpr qreal kMaxZoom = 3.0;
} // namespace

PipelineCanvasWidget::PipelineCanvasWidget( QWidget *parent )
    : QGraphicsView( parent )
{
    m_scene = new PipelineScene( this );
    setScene( m_scene );
    setRenderHint( QPainter::Antialiasing );
    setViewportUpdateMode( QGraphicsView::BoundingRectViewportUpdate );
    setDragMode( QGraphicsView::RubberBandDrag );
    setTransformationAnchor( QGraphicsView::AnchorUnderMouse );

    connect( m_scene, &PipelineScene::connectionCreated, this, &PipelineCanvasWidget::connectionCreated );
    connect( m_scene, &PipelineScene::nodeSelected, this, &PipelineCanvasWidget::nodeSelected );
}

PipelineCanvasWidget::~PipelineCanvasWidget() = default;

void PipelineCanvasWidget::loadWorkflow( const sicnu::workflow::WorkflowDefinition &def )
{
    m_lastLoaded = def;
    m_scene->clear();
    for ( const sicnu::workflow::NodeFact &node : def.nodes )
    {
        auto *item = new PipelineNodeItem( node.nodeId, node.displayName.isEmpty() ? node.operatorId : node.displayName );
        item->setPos( node.canvasPosition );
        for ( const sicnu::workflow::PortFact &port : node.inputPorts )
            item->addInputPort( port.portName );
        for ( const sicnu::workflow::PortFact &port : node.outputPorts )
            item->addOutputPort( port.portName );
        m_scene->addNodeItem( item );
    }
    for ( const sicnu::workflow::EdgeFact &edge : def.edges )
        m_scene->addConnection( edge.sourceNodeId, edge.sourcePortName, edge.targetNodeId, edge.targetPortName );
}

sicnu::workflow::WorkflowDefinition PipelineCanvasWidget::exportWorkflow() const
{
    // The document stays the source of truth: port facts and edges come from
    // the loaded document; only node positions are read back from the items.
    sicnu::workflow::WorkflowDefinition out = m_lastLoaded;

    QHash<QString, QPointF> positions;
    for ( QGraphicsItem *item : m_scene->items() )
        if ( auto *node = qgraphicsitem_cast<PipelineNodeItem *>( item ) )
            positions.insert( node->nodeId(), node->pos() );

    out.nodes.clear();
    for ( const sicnu::workflow::NodeFact &node : m_lastLoaded.nodes )
    {
        sicnu::workflow::NodeFact updated = node;
        if ( positions.contains( node.nodeId ) )
            updated.canvasPosition = positions.value( node.nodeId );
        out.nodes.append( updated );
    }

    // Wiring created on the canvas AFTER loadWorkflow must survive export:
    // append scene connections whose (source, target, ports) tuple is not
    // already in the document. Loaded edges keep their original ids.
    using EdgeKey = QPair<QPair<QString, QString>, QPair<QString, QString>>;
    QSet<EdgeKey> known;
    for ( const sicnu::workflow::EdgeFact &edge : out.edges )
        known.insert( qMakePair( qMakePair( edge.sourceNodeId, edge.sourcePortName ),
                                 qMakePair( edge.targetNodeId, edge.targetPortName ) ) );
    int synthesized = 0;
    for ( QGraphicsItem *item : m_scene->items() )
        if ( auto *connection = qgraphicsitem_cast<PipelineConnectionItem *>( item ) )
        {
            const EdgeKey key = qMakePair( qMakePair( connection->sourceNodeId, connection->sourcePortName ),
                                           qMakePair( connection->targetNodeId, connection->targetPortName ) );
            if ( known.contains( key ) )
                continue;
            known.insert( key );
            out.edges.append( sicnu::workflow::EdgeFact{
                QStringLiteral( "edge_canvas_%1" ).arg( ++synthesized ),
                connection->sourceNodeId, connection->sourcePortName,
                connection->targetNodeId, connection->targetPortName } );
        }
    return out;
}

void PipelineCanvasWidget::setZoomLevel( qreal factor )
{
    factor = std::clamp( factor, kMinZoom, kMaxZoom );
    QTransform transform;
    transform.scale( factor, factor );
    setTransform( transform );
}

qreal PipelineCanvasWidget::zoomLevel() const
{
    return transform().m11();
}

void PipelineCanvasWidget::zoomFitExtent()
{
    fitInView( scene()->itemsBoundingRect(), Qt::KeepAspectRatio );
    if ( zoomLevel() > kMaxZoom || zoomLevel() < kMinZoom )
    {
        const qreal clamped = std::clamp( zoomLevel(), kMinZoom, kMaxZoom );
        setZoomLevel( clamped );
    }
}

} // namespace sicnu::app::pipeline
