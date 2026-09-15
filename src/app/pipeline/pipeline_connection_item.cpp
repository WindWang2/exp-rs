// src/app/pipeline/pipeline_connection_item.cpp — Bézier edge (D17)
#include "pipeline_connection_item.h"

#include <QPainter>

#include <cmath>

namespace sicnu::app::pipeline {

PipelineConnectionItem::PipelineConnectionItem( const QPointF &startPos, const QPointF &endPos,
                                                QGraphicsItem *parent )
    : QObject()
    , QGraphicsPathItem( parent )
{
    setCacheMode( DeviceCoordinateCache );
    setZValue( -1 ); // under nodes
    updateEndpoints( startPos, endPos );
}

QPainterPath PipelineConnectionItem::calculateBezierSpline( const QPointF &p0, const QPointF &p3 )
{
    const qreal dx = std::abs( p3.x() - p0.x() );
    const qreal delta = std::max( 30.0, 0.5 * dx );
    const QPointF p1( p0.x() + delta, p0.y() );
    const QPointF p2( p3.x() - delta, p3.y() );
    QPainterPath path( p0 );
    path.cubicTo( p1, p2, p3 );
    return path;
}

void PipelineConnectionItem::updateEndpoints( const QPointF &startPos, const QPointF &endPos )
{
    setPath( calculateBezierSpline( startPos, endPos ) );
}

void PipelineConnectionItem::paint( QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget )
{
    QPen pen( QColor( 0x53, 0x7d, 0xa6 ), 2.0 );
    pen.setCapStyle( Qt::RoundCap );
    painter->setRenderHint( QPainter::Antialiasing );
    painter->setPen( pen );
    painter->setBrush( Qt::NoBrush );
    QGraphicsPathItem::paint( painter, option, widget );
}

} // namespace sicnu::app::pipeline
