// src/app/pipeline/pipeline_port_item.cpp — port graphics (D17)
#include "pipeline_port_item.h"

#include "pipeline_node_item.h"

#include <QBrush>
#include <QPen>

namespace sicnu::app::pipeline {
namespace {
constexpr qreal kPortRadius = 5.0;
}

PipelinePortItem::PipelinePortItem( PipelineNodeItem *owner, const QString &name, PortDirection dir,
                                    QGraphicsItem *parent )
    : QGraphicsEllipseItem( -kPortRadius, -kPortRadius, 2 * kPortRadius, 2 * kPortRadius, parent )
    , portName( name )
    , direction( dir )
    , ownerNode( owner )
{
    const QColor fill = dir == PortDirection::Input ? QColor( 0x4c, 0xaf, 0x50 ) : QColor( 0xf0, 0x9a, 0x3e );
    setBrush( fill );
    setPen( QPen( Qt::black, 1.0 ) );
    setAcceptHoverEvents( true );
    setZValue( 1 );
}

QPointF PipelinePortItem::sceneCenter() const
{
    return scenePos();
}

} // namespace sicnu::app::pipeline
