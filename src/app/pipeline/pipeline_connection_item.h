// src/app/pipeline/pipeline_connection_item.h — cubic Bézier edge graphics (D17, ADR 0162)
#pragma once

#include <QGraphicsPathItem>
#include <QObject>
#include <QPainterPath>
#include <QPointF>

namespace sicnu::app::pipeline {

/// One wiring edge drawn as a cubic Bézier with horizontal tangents:
///   dx = |x3 - x0|, delta = max(30, 0.5 * dx)
///   P1 = (x0 + delta, y0), P2 = (x3 - delta, y3)
/// `calculateBezierSpline` is a pure static function so tests can verify the
/// analytical midpoint property without instantiating a scene.
/// QObject is a primary base so endpoint-follow lambdas can use the
/// connection as a receiver (and QPointer can track it across node death).
class PipelineConnectionItem : public QObject, public QGraphicsPathItem
{
    Q_OBJECT

  public:
    PipelineConnectionItem( const QPointF &startPos, const QPointF &endPos, QGraphicsItem *parent = nullptr );

    void updateEndpoints( const QPointF &startPos, const QPointF &endPos );

    static QPainterPath calculateBezierSpline( const QPointF &p0, const QPointF &p3 );

    QString sourceNodeId;
    QString sourcePortName;
    QString targetNodeId;
    QString targetPortName;

    enum
    {
        Type = UserType + 3
    };
    int type() const override { return Type; }

  protected:
    void paint( QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget ) override;
};

} // namespace sicnu::app::pipeline
