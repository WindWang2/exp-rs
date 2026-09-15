// src/app/pipeline/pipeline_scene.h — scene: items, snapping, connection drags (D17)
#pragma once

#include <QGraphicsScene>
#include <QHash>
#include <QPointer>
#include <QString>

#include "pipeline_connection_item.h"
#include "pipeline_node_item.h"
#include "pipeline_port_item.h"

namespace sicnu::app::pipeline {

class PipelineConnectionItem;

/// Owns node/port/connection graphics and the interactive wiring state.
/// The document itself stays OUTSIDE the scene: the canvas widget projects
/// WorkflowDocument -> items and items -> WorkflowDocument.
class PipelineScene : public QGraphicsScene
{
    Q_OBJECT

  public:
    explicit PipelineScene( QObject *parent = nullptr );

    static constexpr qreal kSnapRadiusPx = 12.0;

    PipelineNodeItem *nodeItem( const QString &nodeId ) const { return m_nodes.value( nodeId ).data(); }

    /// Adds an item to the scene AND registers it as a named node (the
    /// registry portAtScenePos / nodeItem / addConnection resolve against).
    void addNodeItem( PipelineNodeItem *node );

    /// Adds one connection graphic; weakly tracks endpoints. Returns null
    /// (and adds nothing) when a node id is unknown.
    PipelineConnectionItem *addConnection( const QString &sourceNodeId, const QString &sourcePort,
                                           const QString &targetNodeId, const QString &targetPort );

    /// Nearest input port within the snap radius, or nullptr.
    PipelinePortItem *portAtScenePos( const QPointF &scenePos, qreal maxDistance = kSnapRadiusPx ) const;

    /// True when the Euclidean distance is within the snap threshold — the
    /// exact predicate the drag interaction uses.
    static bool withinSnapRadius( const QPointF &a, const QPointF &b, qreal radius = kSnapRadiusPx );

    /// Live-wire endpoints while the user is dragging a new connection.
    void beginPendingConnection( const QString &sourceNodeId, const QString &sourcePort, const QPointF &start );
    void updatePendingConnection( const QPointF &scenePos );
    /// Commits the pending wire onto @a target; emits connectionCreated.
    bool finishPendingConnection( PipelinePortItem *target );
    void cancelPendingConnection();

    signals:
    void connectionCreated( const QString &sourceNode, const QString &sourcePort,
                            const QString &targetNode, const QString &targetPort );
    void nodeSelected( const QString &nodeId );

  protected:
    void mousePressEvent( QGraphicsSceneMouseEvent *event ) override;
    void mouseMoveEvent( QGraphicsSceneMouseEvent *event ) override;
    void mouseReleaseEvent( QGraphicsSceneMouseEvent *event ) override;

  private:
    QHash<QString, QPointer<PipelineNodeItem>> m_nodes;
    QPointer<PipelineConnectionItem> m_pendingConnection;
    QString m_pendingSourceNode;
    QString m_pendingSourcePort;
};

} // namespace sicnu::app::pipeline
