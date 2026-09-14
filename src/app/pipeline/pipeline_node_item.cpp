// src/app/pipeline/pipeline_node_item.cpp — node graphics (D17)
#include "pipeline_node_item.h"

#include <QBrush>
#include <QFontMetrics>
#include <QPainter>
#include <QPen>

#include <algorithm>

namespace sicnu::app::pipeline {
namespace {
constexpr qreal kPortSpacing = 18.0;
constexpr qreal kPortInset = 12.0;
} // namespace

PipelineNodeItem::PipelineNodeItem( const QString &nodeId, const QString &displayName, QGraphicsItem *parent )
    : QGraphicsObject( parent )
    , m_nodeId( nodeId )
{
    setFlags( ItemIsMovable | ItemIsSelectable | ItemSendsGeometryChanges );
    setCacheMode( DeviceCoordinateCache );
    setZValue( 0 );
    setToolTip( displayName );
}

void PipelineNodeItem::addInputPort( const QString &portName )
{
    auto *port = new PipelinePortItem( this, portName, PortDirection::Input, this );
    const qreal y = kPortInset + m_inputPorts.size() * kPortSpacing;
    port->setPos( 0.0, y );
    m_inputPorts.append( port );
}

void PipelineNodeItem::addOutputPort( const QString &portName )
{
    auto *port = new PipelinePortItem( this, portName, PortDirection::Output, this );
    const qreal y = kPortInset + m_outputPorts.size() * kPortSpacing;
    port->setPos( kWidth, y );
    m_outputPorts.append( port );
}

QPointF PipelineNodeItem::inputPortScenePos( int index ) const
{
    if ( index < 0 || index >= m_inputPorts.size() )
        return scenePos();
    return m_inputPorts[index]->sceneCenter();
}

QPointF PipelineNodeItem::outputPortScenePos( int index ) const
{
    if ( index < 0 || index >= m_outputPorts.size() )
        return scenePos();
    return m_outputPorts[index]->sceneCenter();
}

QString PipelineNodeItem::inputPortName( int index ) const
{
    return index >= 0 && index < m_inputPorts.size() ? m_inputPorts[index]->portName : QString();
}

QString PipelineNodeItem::outputPortName( int index ) const
{
    return index >= 0 && index < m_outputPorts.size() ? m_outputPorts[index]->portName : QString();
}

QRectF PipelineNodeItem::boundingRect() const
{
    // Output ports sit AT x = kWidth with radius 5 and the selected pen is
    // 2 px: the rect must cover them or DeviceCoordinateCache clips them.
    const qreal portRows = std::max( m_inputPorts.size(), m_outputPorts.size() );
    const qreal neededHeight = kPortInset + portRows * kPortSpacing + 8.0;
    constexpr qreal kMargin = 6.0;
    return QRectF( -kMargin, -kMargin, kWidth + 2 * kMargin, std::max( kHeight, neededHeight ) + 2 * kMargin );
}

void PipelineNodeItem::paint( QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget )
{
    Q_UNUSED( option );
    Q_UNUSED( widget );
    painter->setRenderHint( QPainter::Antialiasing );

    const QColor fill = isSelected() ? QColor( 0xd8, 0xea, 0xfc ) : QColor( 0xec, 0xf2, 0xf8 );
    painter->setPen( QPen( QColor( 0x35, 0x62, 0x8f ), isSelected() ? 2.0 : 1.2 ) );
    painter->setBrush( fill );
    painter->drawRoundedRect( QRectF( 0, 0, kWidth, boundingRect().height() ), 8, 8 );

    painter->setPen( Qt::black );
    const QFontMetrics metrics( painter->font() );
    painter->drawText( QRectF( 12, 4, kWidth - 24, 18 ), Qt::AlignVCenter,
                       metrics.elidedText( m_nodeId, Qt::ElideRight, static_cast<int>( kWidth - 26 ) ) );
}

QVariant PipelineNodeItem::itemChange( GraphicsItemChange change, const QVariant &value )
{
    if ( change == ItemPositionHasChanged )
        emit nodeMoved( m_nodeId, value.toPointF() );
    return QGraphicsObject::itemChange( change, value );
}

} // namespace sicnu::app::pipeline
