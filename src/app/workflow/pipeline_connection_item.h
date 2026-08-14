// src/app/workflow/pipeline_connection_item.h
#pragma once

#include <QGraphicsPathItem>
#include <QPointF>

namespace sicnu::workflow::gui {

class PipelinePortItem;

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
  PipelinePortItem *mSourcePort = nullptr;
  PipelinePortItem *mTargetPort = nullptr;
  QPointF mTempEndPoint;
  bool mIsTemp = false;

  mutable QPainterPath mCachedShape;
  mutable bool mShapeDirty = true;

  void rebuildPath( const QPointF &startPos, const QPointF &endPos );
};

} // namespace sicnu::workflow::gui
