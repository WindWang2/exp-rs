// src/app/workflow/pipeline_port_item.cpp
#include "pipeline_port_item.h"
#include "pipeline_connection_item.h"
#include "pipeline_node_item.h"

#include <QGraphicsSceneMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QStyleOptionGraphicsItem>
#include <algorithm>

namespace sicnu::workflow::gui {

PipelinePortItem::PipelinePortItem( const QString &portName,
                                    const QString &portType,
                                    PortDirection direction,
                                    PipelineNodeItem *parentNode,
                                    QGraphicsItem *parent )
  : QGraphicsObject( parent )
  , mPortName( portName )
  , mPortType( portType )
  , mDirection( direction )
  , mNodeItem( parentNode )
{
  setAcceptHoverEvents( true );
}

QColor PipelinePortItem::portTypeColor( const QString &portType )
{
  if ( portType.compare( "Raster", Qt::CaseInsensitive ) == 0 ||
       portType.compare( "RasterLayer", Qt::CaseInsensitive ) == 0 )
  {
    return QColor( "#10b981" ); // Emerald
  }
  if ( portType.compare( "Vector", Qt::CaseInsensitive ) == 0 ||
       portType.compare( "VectorLayer", Qt::CaseInsensitive ) == 0 )
  {
    return QColor( "#0284c7" ); // Sky Blue
  }
  if ( portType.compare( "Number", Qt::CaseInsensitive ) == 0 ||
       portType.compare( "Double", Qt::CaseInsensitive ) == 0 ||
       portType.compare( "Integer", Qt::CaseInsensitive ) == 0 )
  {
    return QColor( "#f59e0b" ); // Amber
  }
  if ( portType.compare( "Any", Qt::CaseInsensitive ) == 0 )
  {
    return QColor( "#8b5cf6" ); // Purple
  }
  return QColor( "#94a3b8" ); // Slate neutral
}

void PipelinePortItem::setAddToMap( bool enabled )
{
  if ( mAddToMap != enabled )
  {
    mAddToMap = enabled;
    update();
    emit addToMapToggled( this, mAddToMap );
  }
}

QPointF PipelinePortItem::sceneAnchorPos() const
{
  qreal pinX = isInput() ? PIN_RADIUS : PORT_WIDTH - PIN_RADIUS;
  qreal pinY = PORT_HEIGHT * 0.5;
  return mapToScene( QPointF( pinX, pinY ) );
}

void PipelinePortItem::addConnection( PipelineConnectionItem *conn )
{
  if ( std::find( mConnections.begin(), mConnections.end(), conn ) == mConnections.end() )
  {
    mConnections.push_back( conn );
  }
}

void PipelinePortItem::removeConnection( PipelineConnectionItem *conn )
{
  auto it = std::remove( mConnections.begin(), mConnections.end(), conn );
  if ( it != mConnections.end() )
  {
    mConnections.erase( it, mConnections.end() );
  }
}

QRectF PipelinePortItem::boundingRect() const
{
  return QRectF( 0, 0, PORT_WIDTH, PORT_HEIGHT );
}

void PipelinePortItem::paint( QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget )
{
  Q_UNUSED( option )
  Q_UNUSED( widget )

  painter->setRenderHint( QPainter::Antialiasing, true );

  QColor accent = portTypeColor( mPortType );
  qreal pinX = isInput() ? PIN_RADIUS : PORT_WIDTH - PIN_RADIUS;
  qreal pinY = PORT_HEIGHT * 0.5;
  QPointF pinCenter( pinX, pinY );

  // Pin Circle
  painter->setPen( QPen( accent.lighter( 120 ), 1.5 ) );
  painter->setBrush( accent );
  painter->drawEllipse( pinCenter, PIN_RADIUS - 1.0, PIN_RADIUS - 1.0 );

  // Label text
  painter->setFont( QFont( "Inter", 9 ) );
  painter->setPen( QColor( "#e2e8f0" ) );

  QString displayLabel = QString( "%1 (%2)" ).arg( mPortName, mPortType );
  QRectF textRect;

  if ( isInput() )
  {
    textRect = QRectF( PIN_RADIUS * 2 + 4, 0, PORT_WIDTH - PIN_RADIUS * 2 - 8, PORT_HEIGHT );
    painter->drawText( textRect, Qt::AlignLeft | Qt::AlignVCenter, displayLabel );
  }
  else
  {
    textRect = QRectF( 24, 0, PORT_WIDTH - PIN_RADIUS * 2 - 28, PORT_HEIGHT );
    painter->drawText( textRect, Qt::AlignRight | Qt::AlignVCenter, displayLabel );

    // 👁️ Add to map toggle icon button for output ports
    QRectF eyeRect( 2, ( PORT_HEIGHT - 16 ) * 0.5, 18, 16 );
    if ( mAddToMap )
    {
      painter->setPen( QPen( QColor( "#38bdf8" ), 1.2 ) );
      painter->setBrush( QColor( "#0284c7" ).lighter( 130 ) );
      painter->drawRoundedRect( eyeRect, 3, 3 );
      painter->setPen( Qt::white );
      painter->setFont( QFont( "Inter", 8, QFont::Bold ) );
      painter->drawText( eyeRect, Qt::AlignCenter, "👁" );
    }
    else
    {
      painter->setPen( QPen( QColor( "#64748b" ), 1.0 ) );
      painter->setBrush( QColor( "#1e293b" ) );
      painter->drawRoundedRect( eyeRect, 3, 3 );
      painter->setPen( QColor( "#64748b" ) );
      painter->setFont( QFont( "Inter", 8 ) );
      painter->drawText( eyeRect, Qt::AlignCenter, "👁" );
    }
  }
}

void PipelinePortItem::mousePressEvent( QGraphicsSceneMouseEvent *event )
{
  if ( event->button() == Qt::LeftButton )
  {
    if ( isOutput() )
    {
      QRectF eyeRect( 2, ( PORT_HEIGHT - 16 ) * 0.5, 18, 16 );
      if ( eyeRect.contains( event->pos() ) )
      {
        setAddToMap( !mAddToMap );
        event->accept();
        return;
      }
    }

    emit connectionDragStarted( this, event->scenePos() );
    event->accept();
    return;
  }
  QGraphicsObject::mousePressEvent( event );
}

} // namespace sicnu::workflow::gui
