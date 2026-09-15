// src/app/pipeline/pipeline_canvas_widget.h — node-graph view over WorkflowDocument (D17)
#pragma once

#include <QGraphicsView>

#include "workflow/workflow_ir_v2.h"

namespace sicnu::app::pipeline {

class PipelineScene;

/// The designer canvas: QGraphicsView bound to WorkflowDocument 2.0.
/// loadWorkflow projects the document onto graphics items; exportWorkflow
/// projects item geometry back. The document is the single source of truth.
/// Zoom is clamped to [0.2, 3.0]; rendering uses DeviceCoordinateCache +
/// BoundingRectViewportUpdate so 100-node documents stay fluid offscreen.
class PipelineCanvasWidget : public QGraphicsView
{
    Q_OBJECT

  public:
    explicit PipelineCanvasWidget( QWidget *parent = nullptr );
    ~PipelineCanvasWidget() override;

    void loadWorkflow( const sicnu::workflow::WorkflowDocument &def );
    sicnu::workflow::WorkflowDocument exportWorkflow() const;

    void setZoomLevel( qreal factor ); // clamped to [0.2, 3.0]
    qreal zoomLevel() const;
    void zoomFitExtent();

    PipelineScene *scene() const { return m_scene; }

    signals:
    void connectionCreated( const QString &sourceNode, const QString &sourcePort,
                            const QString &targetNode, const QString &targetPort );
    void nodeSelected( const QString &nodeId );
    void requestAutoLayout();

  private:
    PipelineScene *m_scene = nullptr;
    sicnu::workflow::WorkflowDocument m_lastLoaded;
};

} // namespace sicnu::app::pipeline
