// src/app/workflow/pipeline_connection_item.cpp
#include "pipeline_connection_item.h"
#include "pipeline_port_item.h"

#include <QPainter>
#include <QPainterPathStroker>
#include <QStyleOptionGraphicsItem>
#include <cmath>

namespace sicnu::workflow::gui {

PipelineConnectionItem::PipelineConnectionItem( PipelinePortItem *sourcePort,
                                                PipelinePortItem *targetPort,
                                                QGraphicsItem *parent )
  : QGraphicsPathItem( parent )
  , mSourcePort( sourcePort )
  , mTargetPort( targetPort )
{
  setFlag( QGraphicsItem::ItemIsSelectable, true );
  setAcceptHoverEvents( true );
  setZValue( -1.0 ); // Draw below nodes

  if ( mSourcePort )
  {
    mSourcePort->addConnection( this );
  }
  if ( mTargetPort )
  {
    mTargetPort->addConnection( this );
  }

  updatePosition();
}

PipelineConnectionItem::~PipelineConnectionItem()
{
  if ( mSourcePort )
  {
    mSourcePort->removeConnection( this );
  }
  if ( mTargetPort )
  {
    mTargetPort->removeConnection( this );
  }
}

void PipelineConnectionItem::setTargetPort( PipelinePortItem *targetPort )
{
  if ( mTargetPort != targetPort )
  {
    if ( mTargetPort )
    {
      mTargetPort->removeConnection( this );
    }
    mTargetPort = targetPort;
    if ( mTargetPort )
    {
      mTargetPort->addConnection( this );
    }
    mIsTemp = false;
    updatePosition();
  }
}

void PipelineConnectionItem::setTempEndPoint( const QPointF &scenePos )
{
  mTempEndPoint = scenePos;
  mIsTemp = true;
  updatePosition();
}

void PipelineConnectionItem::updatePosition()
{
  if ( !mSourcePort )
    return;

  QPointF startPos = mSourcePort->sceneAnchorPos();
  QPointF endPos = ( mTargetPort != nullptr && !mIsTemp ) ? mTargetPort->sceneAnchorPos() : mTempEndPoint;

  rebuildPath( startPos, endPos );
}

void PipelineConnectionItem::rebuildPath( const QPointF &startPos, const QPointF &endPos )
{
  QPainterPath p( startPos );

  qreal dx = std::abs( endPos.x() - startPos.x() ) * 0.5;
  if ( dx < 40.0 )
    dx = 40.0;

  QPointF c1( startPos.x() + dx, startPos.y() );
  QPointF c2( endPos.x() - dx, endPos.y() );

  p.cubicTo( c1, c2, endPos );
  mShapeDirty = true;
  setPath( p );
}

QRectF PipelineConnectionItem::boundingRect() const
{
  // Pad bounding rect by 3.5px to cover maximum selection pen stroke (3.5px) + antialiasing
  return path().boundingRect().adjusted( -3.5, -3.5, 3.5, 3.5 );
}

QPainterPath PipelineConnectionItem::shape() const
{
  if ( mShapeDirty )
  {
    QPainterPathStroker stroker;
    stroker.setWidth( 12.0 ); // Easy hit detection target
    mCachedShape = stroker.createStroke( path() );
    mShapeDirty = false;
  }
  return mCachedShape;
}

void PipelineConnectionItem::paint( QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget )
{
  Q_UNUSED( option )
  Q_UNUSED( widget )

  painter->setRenderHint( QPainter::Antialiasing, true );

  QColor lineColor = QColor( "#38bdf8" ); // Vibrant cyan-blue default
  if ( mSourcePort )
  {
    lineColor = PipelinePortItem::portTypeColor( mSourcePort->portType() );
  }

  qreal penWidth = 2.5;
  if ( isSelected() )
  {
    lineColor = QColor( "#f43f5e" ); // Rose red when selected
    penWidth = 3.5;
  }
  else if ( option->state & QStyle::State_MouseOver )
  {
    lineColor = lineColor.lighter( 140 );
    penWidth = 3.0;
  }

  QPen pen( lineColor, penWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin );
  painter->setPen( pen );
  painter->drawPath( path() );
}

} // namespace sicnu::workflow::gui
