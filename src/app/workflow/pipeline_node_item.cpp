// src/app/workflow/pipeline_node_item.cpp
#include "pipeline_node_item.h"
#include "pipeline_port_item.h"
#include "pipeline_connection_item.h"

#include <QGraphicsSceneMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QStyleOptionGraphicsItem>

namespace sicnu::workflow::gui {

PipelineNodeItem::PipelineNodeItem( const StepDef &stepDef, QGraphicsItem *parent )
  : QGraphicsObject( parent )
  , mStepId( QString::fromStdString( stepDef.id ) )
  , mTitle( QString::fromStdString( stepDef.title.empty() ? stepDef.id : stepDef.title ) )
  , mOperatorId( QString::fromStdString( stepDef.operatorId ) )
  , mKind( stepDef.kind )
{
  setFlags( QGraphicsItem::ItemIsMovable |
            QGraphicsItem::ItemIsSelectable |
            QGraphicsItem::ItemSendsGeometryChanges );

  setPos( stepDef.uiMeta.x, stepDef.uiMeta.y );
  layoutPorts();
}

QColor PipelineNodeItem::statusColor( NodeStatus status )
{
  switch ( status )
  {
    case NodeStatus::Idle:
      return QColor( "#64748b" ); // Slate
    case NodeStatus::Running:
      return QColor( "#3b82f6" ); // Vibrant Blue
    case NodeStatus::Success:
      return QColor( "#22c55e" ); // Emerald Green
    case NodeStatus::Failure:
      return QColor( "#ef4444" ); // Crimson Red
    case NodeStatus::GateWaiting:
      return QColor( "#f59e0b" ); // Amber
  }
  return QColor( "#64748b" );
}

QString PipelineNodeItem::statusText( NodeStatus status )
{
  switch ( status )
  {
    case NodeStatus::Idle:
      return QObject::tr( "空闲" );
    case NodeStatus::Running:
      return QObject::tr( "运行中" );
    case NodeStatus::Success:
      return QObject::tr( "成功" );
    case NodeStatus::Failure:
      return QObject::tr( "失败" );
    case NodeStatus::GateWaiting:
      return QObject::tr( "等待中" );
  }
  return QObject::tr( "未知" );
}

void PipelineNodeItem::setStatus( NodeStatus status )
{
  if ( mStatus != status )
  {
    mStatus = status;
    update();
    emit statusChanged( this, mStatus );
  }
}

void PipelineNodeItem::setStatusFromString( const QString &statusStr )
{
  if ( statusStr.compare( "running", Qt::CaseInsensitive ) == 0 )
    setStatus( NodeStatus::Running );
  else if ( statusStr.compare( "success", Qt::CaseInsensitive ) == 0 ||
            statusStr.compare( "completed", Qt::CaseInsensitive ) == 0 )
    setStatus( NodeStatus::Success );
  else if ( statusStr.compare( "failure", Qt::CaseInsensitive ) == 0 ||
            statusStr.compare( "failed", Qt::CaseInsensitive ) == 0 ||
            statusStr.compare( "error", Qt::CaseInsensitive ) == 0 )
    setStatus( NodeStatus::Failure );
  else if ( statusStr.compare( "waiting", Qt::CaseInsensitive ) == 0 ||
            statusStr.compare( "gate", Qt::CaseInsensitive ) == 0 )
    setStatus( NodeStatus::GateWaiting );
  else
    setStatus( NodeStatus::Idle );
}

PipelinePortItem *PipelineNodeItem::addInputPort( const QString &portName, const QString &portType )
{
  auto *port = new PipelinePortItem( portName, portType, PipelinePortItem::PortDirection::Input, this, this );
  mInputPorts.push_back( port );
  layoutPorts();
  return port;
}

PipelinePortItem *PipelineNodeItem::addOutputPort( const QString &portName, const QString &portType )
{
  auto *port = new PipelinePortItem( portName, portType, PipelinePortItem::PortDirection::Output, this, this );
  mOutputPorts.push_back( port );
  layoutPorts();
  return port;
}

PipelinePortItem *PipelineNodeItem::findInputPort( const QString &portName ) const
{
  for ( auto *port : mInputPorts )
  {
    if ( port->portName() == portName )
      return port;
  }
  return nullptr;
}

PipelinePortItem *PipelineNodeItem::findOutputPort( const QString &portName ) const
{
  for ( auto *port : mOutputPorts )
  {
    if ( port->portName() == portName )
      return port;
  }
  return nullptr;
}

void PipelineNodeItem::layoutPorts()
{
  prepareGeometryChange();

  qreal inHeight = mInputPorts.size() * 28.0;
  qreal outHeight = mOutputPorts.size() * 28.0;
  qreal bodyHeight = std::max( { 40.0, inHeight, outHeight } );
  qreal totalHeight = mHeaderHeight + bodyHeight + 12.0;

  // Margin of 2.0px ensures 2px selection border is completely contained
  mBoundingRect = QRectF( -2.0, -2.0, mWidth + 4.0, totalHeight + 4.0 );

  qreal portHeight = 24.0;
  qreal currentY = mHeaderHeight + 6.0;

  for ( auto *port : mInputPorts )
  {
    port->setPos( 0, currentY );
    currentY += portHeight + 4.0;
  }

  currentY = mHeaderHeight + 6.0;
  for ( auto *port : mOutputPorts )
  {
    port->setPos( mWidth - 140.0, currentY );
    currentY += portHeight + 4.0;
  }

  update();
}

QRectF PipelineNodeItem::boundingRect() const
{
  return mBoundingRect.isEmpty() ? QRectF( -2.0, -2.0, mWidth + 4.0, mHeaderHeight + 56.0 ) : mBoundingRect;
}

void PipelineNodeItem::paint( QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget )
{
  Q_UNUSED( option )
  Q_UNUSED( widget )

  painter->setRenderHint( QPainter::Antialiasing, true );

  qreal totalHeight = mBoundingRect.height() - 4.0;
  QRectF cardRect( 0, 0, mWidth, totalHeight );
  qreal radius = 8.0;

  // Background card gradient
  QLinearGradient bgGrad( cardRect.topLeft(), cardRect.bottomLeft() );
  bgGrad.setColorAt( 0.0, QColor( "#1e293b" ) ); // Slate 800
  bgGrad.setColorAt( 1.0, QColor( "#0f172a" ) ); // Slate 900

  // Selection outline
  QColor borderColor = QColor( "#334155" );
  qreal borderWidth = 1.0;
  if ( isSelected() )
  {
    borderColor = QColor( "#38bdf8" ); // Sky blue focus
    borderWidth = 2.0;
  }

  // Draw Card Container
  painter->setPen( QPen( borderColor, borderWidth ) );
  painter->setBrush( bgGrad );
  painter->drawRoundedRect( cardRect, radius, radius );

  // Header Title Bar
  QRectF headerRect( 0, 0, mWidth, mHeaderHeight );
  QPainterPath headerPath;
  headerPath.addRoundedRect( headerRect, radius, radius );
  // Clip top rounded corners
  QRectF bottomSquare( 0, mHeaderHeight - radius, mWidth, radius );
  headerPath.addRect( bottomSquare );

  QLinearGradient headerGrad( headerRect.topLeft(), headerRect.bottomLeft() );
  headerGrad.setColorAt( 0.0, QColor( "#334155" ) );
  headerGrad.setColorAt( 1.0, QColor( "#1e293b" ) );

  painter->setPen( Qt::NoPen );
  painter->setBrush( headerGrad );
  painter->drawPath( headerPath );

  // Header bottom border line
  painter->setPen( QPen( QColor( "#475569" ), 1.0 ) );
  painter->drawLine( QPointF( 0, mHeaderHeight ), QPointF( mWidth, mHeaderHeight ) );

  // Node Title text
  painter->setFont( QFont( QStringLiteral( "IBM Plex Sans" ), 10, QFont::Bold ) );
  painter->setPen( QColor( "#f8fafc" ) );
  painter->drawText( QRectF( 10, 0, mWidth - 85, mHeaderHeight ), Qt::AlignLeft | Qt::AlignVCenter, mTitle );

  // Status Overlay Badge
  QColor sColor = statusColor( mStatus );
  QString sTxt = statusText( mStatus );

  QRectF badgeRect( mWidth - 75, ( mHeaderHeight - 20 ) * 0.5, 70, 20 );
  painter->setPen( QPen( sColor.lighter( 130 ), 1.0 ) );
  painter->setBrush( sColor );
  painter->drawRoundedRect( badgeRect, 4, 4 );

  painter->setFont( QFont( QStringLiteral( "IBM Plex Sans" ), 8, QFont::Bold ) );
  painter->setPen( Qt::white );
  painter->drawText( badgeRect, Qt::AlignCenter, sTxt );
}

QVariant PipelineNodeItem::itemChange( GraphicsItemChange change, const QVariant &value )
{
  if ( change == ItemPositionHasChanged )
  {
    // Update connected edges position
    for ( auto *port : mInputPorts )
    {
      for ( auto *conn : port->connections() )
      {
        conn->updatePosition();
      }
    }
    for ( auto *port : mOutputPorts )
    {
      for ( auto *conn : port->connections() )
      {
        conn->updatePosition();
      }
    }
    emit positionChanged( this, value.toPointF() );
  }
  return QGraphicsObject::itemChange( change, value );
}

void PipelineNodeItem::mouseDoubleClickEvent( QGraphicsSceneMouseEvent *event )
{
  emit nodeDoubleClicked( this );
  QGraphicsObject::mouseDoubleClickEvent( event );
}

} // namespace sicnu::workflow::gui
