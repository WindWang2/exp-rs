// src/app/pipeline/pipeline_node_item.h — operator node graphics (D17)
#pragma once

#include <QGraphicsObject>
#include <QString>
#include <QVector>

#include "pipeline_port_item.h"

namespace sicnu::app::pipeline {

/// One operator node: a rounded rect with typed input (left) and output
/// (right) ports. Draggable; reports moves so the owning scene can keep the
/// WorkflowDefinition projection in sync. lifecycle note: the scene owns
/// nodes; connections hold QPointer-weak references to their endpoints and
/// self-destruct when either endpoint dies.
class PipelineNodeItem : public QGraphicsObject
{
    Q_OBJECT

  public:
    PipelineNodeItem( const QString &nodeId, const QString &displayName, QGraphicsItem *parent = nullptr );

    QString nodeId() const { return m_nodeId; }

    void addInputPort( const QString &portName );
    void addOutputPort( const QString &portName );

    /// Left edge midpoint in scene coordinates (input anchors), or the node
    /// center when @a fraction is omitted.
    QPointF inputPortScenePos( int index ) const;
    QPointF outputPortScenePos( int index ) const;
    int inputPortCount() const { return m_inputPorts.size(); }
    int outputPortCount() const { return m_outputPorts.size(); }

    QRectF boundingRect() const override;
    void paint( QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget ) override;

    // qgraphicsitem_cast in Qt 6 does NOT type-check — the discriminating
    // enum is mandatory for every scene class in this module.
    enum
    {
        Type = UserType + 1
    };
    int type() const override { return Type; }

  signals:
    void nodeMoved( const QString &nodeId, QPointF newPosition );

  protected:
    QVariant itemChange( GraphicsItemChange change, const QVariant &value ) override;

  private:
    QString m_nodeId;
    // Ports are child items: they die with the node, no QPointer needed.
    QVector<PipelinePortItem *> m_inputPorts;
    QVector<PipelinePortItem *> m_outputPorts;
    static constexpr qreal kWidth = 160.0;
    static constexpr qreal kHeight = 56.0;
};

} // namespace sicnu::app::pipeline
