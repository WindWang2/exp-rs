// src/app/pipeline/pipeline_port_item.h — typed connection port graphics (D17)
#pragma once

#include <QGraphicsEllipseItem>
#include <QString>

namespace sicnu::app::pipeline {

class PipelineNodeItem;

enum class PortDirection
{
    Input,
    Output
};

class PipelinePortItem : public QGraphicsEllipseItem
{
  public:
    PipelinePortItem( PipelineNodeItem *owner, const QString &portName, PortDirection direction,
                      QGraphicsItem *parent = nullptr );

    QString portName;
    PortDirection direction = PortDirection::Input;
    PipelineNodeItem *ownerNode = nullptr;

    /// Scene-coordinate center of the port (the snap and Bezier anchor).
    QPointF sceneCenter() const;

    enum
    {
        Type = UserType + 2
    };
    int type() const override { return Type; }
};

} // namespace sicnu::app::pipeline
