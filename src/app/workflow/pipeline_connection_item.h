// src/app/workflow/pipeline_connection_item.h
#pragma once

#include <QGraphicsPathItem>
#include <QPointF>
#include <QPointer>

#include "pipeline_port_item.h"

namespace sicnu::workflow::gui {

class PipelineConnectionItem : public QGraphicsPathItem
{
public:
  explicit PipelineConnectionItem( PipelinePortItem *sourcePort,
                                    PipelinePortItem *targetPort = nullptr,
                                    QGraphicsItem *parent = nullptr );

  ~PipelineConnectionItem() override;

  PipelinePortItem *sourcePort() const { return mSourcePort; }
  PipelinePortItem *targetPort() const { return mTargetPort; }
  void setTargetPort( PipelinePortItem *targetPort );

  void setTempEndPoint( const QPointF &scenePos );
  void updatePosition();

  enum { Type = UserType + 103 };
  int type() const override { return Type; }

  void paint( QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget ) override;
  QRectF boundingRect() const override;
  QPainterPath shape() const override;

private:
  // QPointer: scene mutation deletes ports while an edge may still be alive
  // (most notably the in-flight temp connection). A destroyed port nulls these
  // automatically, so paint()/updatePosition()/the destructor can never touch
  // freed memory even if a future code path forgets to unregister first.
  QPointer<PipelinePortItem> mSourcePort;
  QPointer<PipelinePortItem> mTargetPort;
  QPointF mTempEndPoint;
  bool mIsTemp = false;

  mutable QPainterPath mCachedShape;
  mutable bool mShapeDirty = true;

  void rebuildPath( const QPointF &startPos, const QPointF &endPos );
};

} // namespace sicnu::workflow::gui
