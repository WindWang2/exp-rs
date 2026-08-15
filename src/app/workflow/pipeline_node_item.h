// src/app/workflow/pipeline_node_item.h
#pragma once

#include <QGraphicsObject>
#include <QColor>
#include <QString>
#include <vector>

#include "workflow_types.h"

namespace sicnu::workflow::gui {

class PipelinePortItem;

enum class NodeStatus
{
  Idle,
  Running,
  Success,
  Failure,
  GateWaiting
};

class PipelineNodeItem : public QGraphicsObject
{
  Q_OBJECT

public:
  explicit PipelineNodeItem( const StepDef &stepDef, QGraphicsItem *parent = nullptr );
  ~PipelineNodeItem() override = default;

  QString stepId() const { return mStepId; }
  QString title() const { return mTitle; }
  QString operatorId() const { return mOperatorId; }
  StepKind stepKind() const { return mKind; }

  NodeStatus status() const { return mStatus; }
  void setStatus( NodeStatus status );
  void setStatusFromString( const QString &statusStr );

  PipelinePortItem *addInputPort( const QString &portName, const QString &portType );
  PipelinePortItem *addOutputPort( const QString &portName, const QString &portType );

  PipelinePortItem *findInputPort( const QString &portName ) const;
  PipelinePortItem *findOutputPort( const QString &portName ) const;

  const std::vector<PipelinePortItem *> &inputPorts() const { return mInputPorts; }
  const std::vector<PipelinePortItem *> &outputPorts() const { return mOutputPorts; }

  void layoutPorts();

  enum { Type = UserType + 101 };
  int type() const override { return Type; }

  QRectF boundingRect() const override;
  void paint( QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget ) override;

signals:
  void positionChanged( PipelineNodeItem *node, const QPointF &newPos );
  void statusChanged( PipelineNodeItem *node, NodeStatus status );
  void nodeDoubleClicked( PipelineNodeItem *node );

protected:
  QVariant itemChange( GraphicsItemChange change, const QVariant &value ) override;
  void mouseDoubleClickEvent( QGraphicsSceneMouseEvent *event ) override;

private:
  QString mStepId;
  QString mTitle;
  QString mOperatorId;
  StepKind mKind = StepKind::Operator;
  NodeStatus mStatus = NodeStatus::Idle;

  std::vector<PipelinePortItem *> mInputPorts;
  std::vector<PipelinePortItem *> mOutputPorts;

  QRectF mBoundingRect;
  qreal mWidth = 240.0;
  qreal mHeaderHeight = 36.0;

  static QColor statusColor( NodeStatus status );
  static QString statusText( NodeStatus status );
};

} // namespace sicnu::workflow::gui
