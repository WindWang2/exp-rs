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

  void paint( QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget ) override;
  QPainterPath shape() const override;

private:
  PipelinePortItem *mSourcePort = nullptr;
  PipelinePortItem *mTargetPort = nullptr;
  QPointF mTempEndPoint;
  bool mIsTemp = false;

  void rebuildPath( const QPointF &startPos, const QPointF &endPos );
};

} // namespace sicnu::workflow::gui
