// src/app/workflow/pipeline_port_item.h
#pragma once

#include <QGraphicsObject>
#include <QColor>
#include <QString>

namespace sicnu::workflow::gui {

class PipelineNodeItem;
class PipelineConnectionItem;

class PipelinePortItem : public QGraphicsObject
{
  Q_OBJECT

public:
  enum class PortDirection { Input, Output };

  PipelinePortItem( const QString &portName,
                    const QString &portType,
                    PortDirection direction,
                    PipelineNodeItem *parentNode,
                    QGraphicsItem *parent = nullptr );

  ~PipelinePortItem() override = default;

  QString portName() const { return mPortName; }
  QString portType() const { return mPortType; }
  PortDirection direction() const { return mDirection; }
  bool isOutput() const { return mDirection == PortDirection::Output; }
  bool isInput() const { return mDirection == PortDirection::Input; }

  PipelineNodeItem *nodeItem() const { return mNodeItem; }

  bool addToMap() const { return mAddToMap; }
  void setAddToMap( bool enabled );

  QPointF sceneAnchorPos() const;
  static QColor portTypeColor( const QString &portType );

  void addConnection( PipelineConnectionItem *conn );
  void removeConnection( PipelineConnectionItem *conn );
  const std::vector<PipelineConnectionItem *> &connections() const { return mConnections; }

  enum { Type = UserType + 102 };
  int type() const override { return Type; }

  QRectF boundingRect() const override;
  QPainterPath shape() const override;
  void paint( QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget ) override;

signals:
  void addToMapToggled( PipelinePortItem *port, bool enabled );
  void connectionDragStarted( PipelinePortItem *port, const QPointF &scenePos );

protected:
  void mousePressEvent( QGraphicsSceneMouseEvent *event ) override;

private:
  QString mPortName;
  QString mPortType;
  QString mDisplayLabel;
  QColor mPortColor;
  PortDirection mDirection;
  bool mAddToMap = false;
  PipelineNodeItem *mNodeItem = nullptr;
  std::vector<PipelineConnectionItem *> mConnections;

  static constexpr qreal PIN_RADIUS = 6.0;
  static constexpr qreal PORT_HEIGHT = 24.0;
  static constexpr qreal PORT_WIDTH = 135.0;
};

} // namespace sicnu::workflow::gui
