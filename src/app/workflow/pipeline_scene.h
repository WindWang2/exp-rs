// src/app/workflow/pipeline_scene.h
#pragma once

#include <QGraphicsScene>
#include <unordered_map>
#include <unordered_set>
#include <memory>

#include "pipeline_node_item.h"
#include "pipeline_port_item.h"
#include "pipeline_connection_item.h"
#include "workflow_types.h"

namespace sicnu::workflow::gui {

class PipelineScene : public QGraphicsScene
{
  Q_OBJECT

public:
  explicit PipelineScene( QObject *parent = nullptr );
  ~PipelineScene() override;

  PipelineNodeItem *addNode( const StepDef &stepDef );
  bool removeNode( const QString &stepId );
  PipelineNodeItem *findNode( const QString &stepId ) const;

  PipelineConnectionItem *addConnection( const QString &fromStepId,
                                          const QString &fromPort,
                                          const QString &toStepId,
                                          const QString &toPort );
  bool removeConnection( PipelineConnectionItem *conn );
  void cancelTempConnection();

  void clearWorkflow();
  void loadWorkflowDefinition( const WorkflowDefinition &def );
  WorkflowDefinition exportWorkflowDefinition( const WorkflowDefinition &baseDef ) const;

  bool isBulkUpdating() const { return mBulkUpdating; }
  const std::unordered_map<std::string, PipelineNodeItem *> &nodes() const { return mNodes; }
  const std::unordered_set<PipelineConnectionItem *> &connections() const { return mConnections; }

signals:
  void workflowChanged();
  void connectionCreated( const QString &fromStepId, const QString &fromPort, const QString &toStepId, const QString &toPort );
  void connectionRemoved( const QString &fromStepId, const QString &fromPort, const QString &toStepId, const QString &toPort );
  void nodePositionChanged( const QString &stepId, double x, double y );
  void nodeDoubleClicked( const QString &stepId );
  void portAddToMapToggled( const QString &stepId, const QString &portName, bool enabled );

protected:
  void drawBackground( QPainter *painter, const QRectF &rect ) override;
  void mouseMoveEvent( QGraphicsSceneMouseEvent *event ) override;
  void mouseReleaseEvent( QGraphicsSceneMouseEvent *event ) override;

private slots:
  void onPortDragStarted( PipelinePortItem *port, const QPointF &scenePos );
  void onNodePositionChanged( PipelineNodeItem *node, const QPointF &newPos );

private:
  void notifyWorkflowChanged();

  std::unordered_map<std::string, PipelineNodeItem *> mNodes;
  std::unordered_set<PipelineConnectionItem *> mConnections;

  bool mBulkUpdating = false;
  PipelinePortItem *mDragSourcePort = nullptr;
  PipelineConnectionItem *mTempConnection = nullptr;
};

} // namespace sicnu::workflow::gui
