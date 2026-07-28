// src/app/workflow/pipeline_canvas_widget.h
#pragma once

#include <QGraphicsView>
#include <QMenu>

#include "pipeline_scene.h"
#include "workflow_types.h"

namespace sicnu::workflow::gui {

class PipelineCanvasWidget : public QGraphicsView
{
  Q_OBJECT

public:
  explicit PipelineCanvasWidget( QWidget *parent = nullptr );
  ~PipelineCanvasWidget() override = default;

  PipelineScene *pipelineScene() const { return mScene; }

  void loadWorkflowDefinition( const WorkflowDefinition &def );
  WorkflowDefinition exportWorkflowDefinition( const WorkflowDefinition &baseDef ) const;

  void updateStepStatus( const QString &stepId, const QString &statusStr );

public slots:
  void zoomIn();
  void zoomOut();
  void zoomToFit();
  void resetZoom();

signals:
  void workflowChanged();
  void runUpToNodeRequested( const QString &stepId );
  void viewNodeLogsRequested( const QString &stepId );
  void nodeSelected( const QString &stepId );

protected:
  void wheelEvent( QWheelEvent *event ) override;
  void mousePressEvent( QMouseEvent *event ) override;
  void mouseMoveEvent( QMouseEvent *event ) override;
  void mouseReleaseEvent( QMouseEvent *event ) override;
  void contextMenuEvent( QContextMenuEvent *event ) override;

private:
  PipelineScene *mScene = nullptr;
  bool mIsPanning = false;
  QPoint mPanStartPos;
  double mZoomFactor = 1.0;

  static constexpr double MIN_ZOOM = 0.2;
  static constexpr double MAX_ZOOM = 3.0;
};

} // namespace sicnu::workflow::gui
