// src/app/workflow/pipeline_scene.cpp
#include "pipeline_scene.h"
#include "workflow_definition.h"

#include <QGraphicsSceneMouseEvent>
#include <QPainter>
#include <cmath>

namespace sicnu::workflow::gui {

PipelineScene::PipelineScene( QObject *parent )
  : QGraphicsScene( parent )
{
  setSceneRect( -2000, -2000, 4000, 4000 );
}

PipelineScene::~PipelineScene()
{
  // QGraphicsScene::clear() deletes items in insertion order; nodes (and their
  // child ports) would be destroyed before connections, whose destructor
  // unregisters itself from its (then-freed) ports. Tear connections down
  // first so no port vector is touched after its owner is gone.
  for ( PipelineConnectionItem *conn : mConnections )
    delete conn;
  mConnections.clear();
  if ( mTempConnection )
  {
    delete mTempConnection;
    mTempConnection = nullptr;
  }
}

PipelineNodeItem *PipelineScene::addNode( const StepDef &stepDef )
{
  if ( findNode( QString::fromStdString( stepDef.id ) ) )
    return nullptr;

  auto *node = new PipelineNodeItem( stepDef );
  addItem( node );
  mNodes[stepDef.id] = node;

  connect( node, &PipelineNodeItem::positionChanged, this, &PipelineScene::onNodePositionChanged );
  connect( node, &PipelineNodeItem::nodeDoubleClicked, this, [this]( PipelineNodeItem *n ) {
    emit nodeDoubleClicked( n->stepId() );
  } );

  // Default input/output ports based on operator/kind if none provided
  if ( stepDef.inputs.empty() )
  {
    node->addInputPort( "input", "Raster" );
  }
  else
  {
    for ( const auto &conn : stepDef.inputs )
    {
      QString toPortName = QString::fromStdString( conn.toPort.empty() ? "input" : conn.toPort );
      if ( !node->findInputPort( toPortName ) )
      {
        node->addInputPort( toPortName, "Raster" );
      }
    }
  }

  QString outPortName = QString::fromStdString( stepDef.artifactOnSuccess.empty() ? "output" : stepDef.artifactOnSuccess );
  auto *outPort = node->addOutputPort( outPortName, "Raster" );

  connect( outPort, &PipelinePortItem::connectionDragStarted, this, &PipelineScene::onPortDragStarted );
  connect( outPort, &PipelinePortItem::addToMapToggled, this, [this, stepDef]( PipelinePortItem *p, bool enabled ) {
    emit portAddToMapToggled( QString::fromStdString( stepDef.id ), p->portName(), enabled );
  } );

  for ( auto *inPort : node->inputPorts() )
  {
    connect( inPort, &PipelinePortItem::connectionDragStarted, this, &PipelineScene::onPortDragStarted );
  }

  emit workflowChanged();
  return node;
}

PipelineNodeItem *PipelineScene::findNode( const QString &stepId ) const
{
  auto it = mNodes.find( stepId.toStdString() );
  if ( it != mNodes.end() )
    return it->second;
  return nullptr;
}

bool PipelineScene::removeNode( const QString &stepId )
{
  auto *node = findNode( stepId );
  if ( !node )
    return false;

  // Delete all connections attached to this node
  std::vector<PipelineConnectionItem *> toDelete;
  for ( auto *conn : mConnections )
  {
    if ( ( conn->sourcePort() && conn->sourcePort()->nodeItem() == node ) ||
         ( conn->targetPort() && conn->targetPort()->nodeItem() == node ) )
    {
      toDelete.push_back( conn );
    }
  }
  for ( auto *conn : toDelete )
  {
    removeConnection( conn );
  }

  mNodes.erase( stepId.toStdString() );
  removeItem( node );
  delete node;

  emit workflowChanged();
  return true;
}

PipelineConnectionItem *PipelineScene::addConnection( const QString &fromStepId,
                                                        const QString &fromPort,
                                                        const QString &toStepId,
                                                        const QString &toPort )
{
  auto *srcNode = findNode( fromStepId );
  auto *dstNode = findNode( toStepId );
  if ( !srcNode || !dstNode )
    return nullptr;

  auto *srcPort = srcNode->findOutputPort( fromPort );
  if ( !srcPort )
    srcPort = srcNode->outputPorts().empty() ? nullptr : srcNode->outputPorts().front();

  auto *dstPort = dstNode->findInputPort( toPort );
  if ( !dstPort )
    dstPort = dstNode->findInputPort( "input" );

  if ( !srcPort || !dstPort )
    return nullptr;

  auto *conn = new PipelineConnectionItem( srcPort, dstPort );
  addItem( conn );
  mConnections.push_back( conn );

  emit connectionCreated( fromStepId, srcPort->portName(), toStepId, dstPort->portName() );
  emit workflowChanged();
  return conn;
}

bool PipelineScene::removeConnection( PipelineConnectionItem *conn )
{
  auto it = std::find( mConnections.begin(), mConnections.end(), conn );
  if ( it == mConnections.end() )
    return false;

  QString fromStep = conn->sourcePort() ? conn->sourcePort()->nodeItem()->stepId() : "";
  QString fromPort = conn->sourcePort() ? conn->sourcePort()->portName() : "";
  QString toStep = conn->targetPort() ? conn->targetPort()->nodeItem()->stepId() : "";
  QString toPort = conn->targetPort() ? conn->targetPort()->portName() : "";

  mConnections.erase( it );
  removeItem( conn );
  delete conn;

  emit connectionRemoved( fromStep, fromPort, toStep, toPort );
  emit workflowChanged();
  return true;
}

void PipelineScene::clearWorkflow()
{
  for ( auto *conn : mConnections )
  {
    removeItem( conn );
    delete conn;
  }
  mConnections.clear();

  for ( const auto &[id, node] : mNodes )
  {
    removeItem( node );
    delete node;
  }
  mNodes.clear();

  emit workflowChanged();
}

void PipelineScene::loadWorkflowDefinition( const WorkflowDefinition &def )
{
  clearWorkflow();

  for ( const auto &stepDef : def.steps )
  {
    auto *node = addNode( stepDef );
    if ( node )
    {
      for ( const auto &[pName, enabled] : stepDef.uiMeta.portAddToMap )
      {
        auto *outPort = node->findOutputPort( QString::fromStdString( pName ) );
        if ( outPort )
        {
          outPort->setAddToMap( enabled );
        }
      }
    }
  }

  for ( const auto &stepDef : def.steps )
  {
    for ( const auto &conn : stepDef.inputs )
    {
      if ( !conn.fromStepId.empty() )
      {
        addConnection( QString::fromStdString( conn.fromStepId ),
                       QString::fromStdString( conn.fromPort ),
                       QString::fromStdString( stepDef.id ),
                       QString::fromStdString( conn.toPort ) );
      }
    }
  }
}

WorkflowDefinition PipelineScene::exportWorkflowDefinition( const WorkflowDefinition &baseDef ) const
{
  WorkflowDefinition outDef = baseDef;
  outDef.steps.clear();

  for ( const auto &[id, node] : mNodes )
  {
    StepDef step;
    step.id = node->stepId().toStdString();
    step.title = node->title().toStdString();
    step.operatorId = node->operatorId().toStdString();
    step.kind = node->stepKind();

    step.uiMeta.x = node->pos().x();
    step.uiMeta.y = node->pos().y();

    for ( const auto *outPort : node->outputPorts() )
    {
      if ( outPort && outPort->addToMap() )
      {
        step.uiMeta.portAddToMap[outPort->portName().toStdString()] = true;
      }
    }

    if ( !node->outputPorts().empty() )
    {
      step.artifactOnSuccess = node->outputPorts().front()->portName().toStdString();
    }

    // Collect incoming connections
    for ( const auto *conn : mConnections )
    {
      if ( conn->targetPort() && conn->targetPort()->nodeItem() == node && conn->sourcePort() )
      {
        StepConnection inConn;
        inConn.fromStepId = conn->sourcePort()->nodeItem()->stepId().toStdString();
        inConn.fromPort = conn->sourcePort()->portName().toStdString();
        inConn.toPort = conn->targetPort()->portName().toStdString();
        step.inputs.push_back( inConn );
      }
    }

    outDef.steps.push_back( step );
  }

  return outDef;
}

void PipelineScene::onPortDragStarted( PipelinePortItem *port, const QPointF &scenePos )
{
  if ( !port || !port->isOutput() )
    return;

  mDragSourcePort = port;
  mTempConnection = new PipelineConnectionItem( port );
  mTempConnection->setTempEndPoint( scenePos );
  addItem( mTempConnection );
}

void PipelineScene::onNodePositionChanged( PipelineNodeItem *node, const QPointF &newPos )
{
  if ( node )
  {
    emit nodePositionChanged( node->stepId(), newPos.x(), newPos.y() );
  }
}

void PipelineScene::mouseMoveEvent( QGraphicsSceneMouseEvent *event )
{
  if ( mTempConnection && mDragSourcePort )
  {
    mTempConnection->setTempEndPoint( event->scenePos() );
    event->accept();
    return;
  }
  QGraphicsScene::mouseMoveEvent( event );
}

void PipelineScene::mouseReleaseEvent( QGraphicsSceneMouseEvent *event )
{
  if ( mTempConnection && mDragSourcePort )
  {
    QGraphicsItem *targetItem = itemAt( event->scenePos(), QTransform() );
    PipelinePortItem *targetPort = dynamic_cast<PipelinePortItem *>( targetItem );

    if ( targetPort && targetPort->isInput() && targetPort->nodeItem() != mDragSourcePort->nodeItem() )
    {
      std::string srcType = mDragSourcePort->portType().toStdString();
      std::string dstType = targetPort->portType().toStdString();

      if ( validatePortConnection( srcType, dstType ) )
      {
        mTempConnection->setTargetPort( targetPort );
        mConnections.push_back( mTempConnection );

        emit connectionCreated( mDragSourcePort->nodeItem()->stepId(),
                                mDragSourcePort->portName(),
                                targetPort->nodeItem()->stepId(),
                                targetPort->portName() );
        emit workflowChanged();

        mTempConnection = nullptr;
        mDragSourcePort = nullptr;
        event->accept();
        return;
      }
    }

    // Invalid connection or dropped in empty space -> cancel temp connection
    removeItem( mTempConnection );
    delete mTempConnection;
    mTempConnection = nullptr;
    mDragSourcePort = nullptr;
    event->accept();
    return;
  }
  QGraphicsScene::mouseReleaseEvent( event );
}

void PipelineScene::drawBackground( QPainter *painter, const QRectF &rect )
{
  painter->fillRect( rect, QColor( "#0f172a" ) ); // Deep navy background

  qreal gridStep = 25.0;
  qreal left = std::floor( rect.left() / gridStep ) * gridStep;
  qreal top = std::floor( rect.top() / gridStep ) * gridStep;

  QPen minorPen( QColor( 255, 255, 255, 12 ), 1.0 );
  QPen majorPen( QColor( 255, 255, 255, 28 ), 1.0 );

  for ( qreal x = left; x < rect.right(); x += gridStep )
  {
    int ix = static_cast<int>( std::round( x ) );
    painter->setPen( ( ix % 100 == 0 ) ? majorPen : minorPen );
    painter->drawLine( QPointF( x, rect.top() ), QPointF( x, rect.bottom() ) );
  }

  for ( qreal y = top; y < rect.bottom(); y += gridStep )
  {
    int iy = static_cast<int>( std::round( y ) );
    painter->setPen( ( iy % 100 == 0 ) ? majorPen : minorPen );
    painter->drawLine( QPointF( rect.left(), y ), QPointF( rect.right(), y ) );
  }
}

} // namespace sicnu::workflow::gui
