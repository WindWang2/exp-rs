#include "pipeline_scene.h"
#include "workflow_definition.h"

#include <QGraphicsSceneMouseEvent>
#include <QPainter>
#include <QVarLengthArray>
#include <cmath>
#include <algorithm>

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

void PipelineScene::notifyWorkflowChanged()
{
  if ( !mBulkUpdating )
  {
    emit workflowChanged();
  }
}

void PipelineScene::cancelTempConnection()
{
  if ( mTempConnection )
  {
    removeItem( mTempConnection );
    delete mTempConnection;
    mTempConnection = nullptr;
    mDragSourcePort = nullptr;
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

  notifyWorkflowChanged();
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

  // Delete only connections incident to this node directly from its ports (O(deg(v)))
  std::vector<PipelineConnectionItem *> toDelete;
  for ( auto *inPort : node->inputPorts() )
  {
    if ( inPort )
    {
      for ( auto *conn : inPort->connections() )
      {
        if ( conn )
          toDelete.push_back( conn );
      }
    }
  }
  for ( auto *outPort : node->outputPorts() )
  {
    if ( outPort )
    {
      for ( auto *conn : outPort->connections() )
      {
        if ( conn )
          toDelete.push_back( conn );
      }
    }
  }

  // Deduplicate in case of multi-referenced edges
  std::sort( toDelete.begin(), toDelete.end() );
  toDelete.erase( std::unique( toDelete.begin(), toDelete.end() ), toDelete.end() );

  for ( auto *conn : toDelete )
  {
    removeConnection( conn );
  }

  mNodes.erase( stepId.toStdString() );
  removeItem( node );
  delete node;

  notifyWorkflowChanged();
  return true;
}

PipelineConnectionItem *PipelineScene::addConnection( const QString &fromStepId,
                                                        const QString &fromPort,
                                                        const QString &toStepId,
                                                        const QString &toPort )
{
  if ( fromStepId == toStepId )
    return nullptr;

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

  // Prevent duplicate connections between the same pair of ports
  for ( auto *existing : dstPort->connections() )
  {
    if ( existing && existing->sourcePort() == srcPort )
    {
      return existing;
    }
  }

  auto *conn = new PipelineConnectionItem( srcPort, dstPort );
  addItem( conn );
  mConnections.insert( conn );

  emit connectionCreated( fromStepId, srcPort->portName(), toStepId, dstPort->portName() );
  notifyWorkflowChanged();
  return conn;
}

bool PipelineScene::removeConnection( PipelineConnectionItem *conn )
{
  if ( !conn )
    return false;

  auto it = mConnections.find( conn );
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
  notifyWorkflowChanged();
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

  notifyWorkflowChanged();
}

void PipelineScene::loadWorkflowDefinition( const WorkflowDefinition &def )
{
  mBulkUpdating = true;
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

  mBulkUpdating = false;
  emit workflowChanged();
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

    // Collect incoming connections directly from node input ports in O(V + E)
    for ( const auto *inPort : node->inputPorts() )
    {
      if ( !inPort )
        continue;
      for ( const auto *conn : inPort->connections() )
      {
        if ( conn && conn->sourcePort() && conn->sourcePort()->nodeItem() )
        {
          StepConnection inConn;
          inConn.fromStepId = conn->sourcePort()->nodeItem()->stepId().toStdString();
          inConn.fromPort = conn->sourcePort()->portName().toStdString();
          inConn.toPort = inPort->portName().toStdString();
          step.inputs.push_back( inConn );
        }
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

      bool alreadyConnected = false;
      for ( auto *existing : targetPort->connections() )
      {
        if ( existing && existing->sourcePort() == mDragSourcePort )
        {
          alreadyConnected = true;
          break;
        }
      }

      if ( !alreadyConnected && validatePortConnection( srcType, dstType ) )
      {
        mTempConnection->setTargetPort( targetPort );
        mConnections.insert( mTempConnection );

        emit connectionCreated( mDragSourcePort->nodeItem()->stepId(),
                                mDragSourcePort->portName(),
                                targetPort->nodeItem()->stepId(),
                                targetPort->portName() );
        notifyWorkflowChanged();

        mTempConnection = nullptr;
        mDragSourcePort = nullptr;
        event->accept();
        return;
      }
    }

    // Invalid connection or dropped in empty space -> cancel temp connection
    cancelTempConnection();
    event->accept();
    return;
  }
  QGraphicsScene::mouseReleaseEvent( event );
}

void PipelineScene::drawBackground( QPainter *painter, const QRectF &rect )
{
  static const QColor bgColor( "#0f172a" ); // Deep slate background
  static const QPen minorPen( QColor( 255, 255, 255, 12 ), 1.0 );
  static const QPen majorPen( QColor( 255, 255, 255, 28 ), 1.0 );

  painter->fillRect( rect, bgColor );

  constexpr qreal gridStep = 25.0;
  const qreal left = std::floor( rect.left() / gridStep ) * gridStep;
  const qreal top = std::floor( rect.top() / gridStep ) * gridStep;

  QVarLengthArray<QLineF, 128> minorLines;
  QVarLengthArray<QLineF, 32> majorLines;

  for ( qreal x = left; x < rect.right(); x += gridStep )
  {
    int ix = static_cast<int>( std::round( x ) );
    QLineF line( x, rect.top(), x, rect.bottom() );
    if ( ix % 100 == 0 )
      majorLines.append( line );
    else
      minorLines.append( line );
  }

  for ( qreal y = top; y < rect.bottom(); y += gridStep )
  {
    int iy = static_cast<int>( std::round( y ) );
    QLineF line( rect.left(), y, rect.right(), y );
    if ( iy % 100 == 0 )
      majorLines.append( line );
    else
      minorLines.append( line );
  }

  if ( !minorLines.isEmpty() )
  {
    painter->setPen( minorPen );
    painter->drawLines( minorLines.data(), minorLines.size() );
  }
  if ( !majorLines.isEmpty() )
  {
    painter->setPen( majorPen );
    painter->drawLines( majorLines.data(), majorLines.size() );
  }

  // Draw empty state watermark placeholder when scene has 0 nodes
  if ( mNodes.empty() )
  {
    painter->setPen( QColor( 148, 163, 184, 110 ) );
    painter->setFont( QFont( QStringLiteral( "IBM Plex Sans" ), 12, QFont::DemiBold ) );
    QRectF hintRect( -250, -50, 500, 100 );
    painter->drawText( hintRect, Qt::AlignCenter, tr( "工作流画布为空\n从右侧选择预设模板或使用工具栏构建流程" ) );
  }
}

} // namespace sicnu::workflow::gui
