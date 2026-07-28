// src/app/workflow/pipeline_canvas_widget.cpp
#include "pipeline_canvas_widget.h"

#include <QContextMenuEvent>
#include <QMouseEvent>
#include <QScrollBar>
#include <QWheelEvent>
#include <cmath>

namespace sicnu::workflow::gui {

PipelineCanvasWidget::PipelineCanvasWidget( QWidget *parent )
  : QGraphicsView( parent )
  , mScene( new PipelineScene( this ) )
{
  setScene( mScene );
  setRenderHint( QPainter::Antialiasing, true );
  setRenderHint( QPainter::SmoothPixmapTransform, true );
  setViewportUpdateMode( QGraphicsView::FullViewportUpdate );

  setHorizontalScrollBarPolicy( Qt::ScrollBarAsNeeded );
  setVerticalScrollBarPolicy( Qt::ScrollBarAsNeeded );
  setTransformationAnchor( QGraphicsView::AnchorUnderMouse );
  setResizeAnchor( QGraphicsView::AnchorUnderMouse );
  setDragMode( QGraphicsView::RubberBandDrag );

  connect( mScene, &PipelineScene::workflowChanged, this, &PipelineCanvasWidget::workflowChanged );
}

void PipelineCanvasWidget::loadWorkflowDefinition( const WorkflowDefinition &def )
{
  mScene->loadWorkflowDefinition( def );
  zoomToFit();
}

WorkflowDefinition PipelineCanvasWidget::exportWorkflowDefinition( const WorkflowDefinition &baseDef ) const
{
  return mScene->exportWorkflowDefinition( baseDef );
}

void PipelineCanvasWidget::updateStepStatus( const QString &stepId, const QString &statusStr )
{
  auto *node = mScene->findNode( stepId );
  if ( node )
  {
    node->setStatusFromString( statusStr );
  }
}

void PipelineCanvasWidget::zoomIn()
{
  if ( mZoomFactor < MAX_ZOOM )
  {
    scale( 1.15, 1.15 );
    mZoomFactor *= 1.15;
  }
}

void PipelineCanvasWidget::zoomOut()
{
  if ( mZoomFactor > MIN_ZOOM )
  {
    scale( 1.0 / 1.15, 1.0 / 1.15 );
    mZoomFactor /= 1.15;
  }
}

void PipelineCanvasWidget::resetZoom()
{
  resetTransform();
  mZoomFactor = 1.0;
}

void PipelineCanvasWidget::zoomToFit()
{
  QRectF bounds = mScene->itemsBoundingRect();
  if ( !bounds.isEmpty() )
  {
    bounds.adjust( -50, -50, 50, 50 );
    fitInView( bounds, Qt::KeepAspectRatio );
    mZoomFactor = transform().m11();
  }
}

void PipelineCanvasWidget::wheelEvent( QWheelEvent *event )
{
  if ( event->modifiers() & Qt::ControlModifier )
  {
    if ( event->angleDelta().y() > 0 )
      zoomIn();
    else
      zoomOut();
    event->accept();
  }
  else
  {
    QGraphicsView::wheelEvent( event );
  }
}

void PipelineCanvasWidget::mousePressEvent( QMouseEvent *event )
{
  if ( event->button() == Qt::MiddleButton || ( event->button() == Qt::LeftButton && event->modifiers() & Qt::AltModifier ) )
  {
    mIsPanning = true;
    mPanStartPos = event->pos();
    setCursor( Qt::ClosedHandCursor );
    event->accept();
    return;
  }
  QGraphicsView::mousePressEvent( event );
}

void PipelineCanvasWidget::mouseMoveEvent( QMouseEvent *event )
{
  if ( mIsPanning )
  {
    QPoint delta = event->pos() - mPanStartPos;
    mPanStartPos = event->pos();
    horizontalScrollBar()->setValue( horizontalScrollBar()->value() - delta.x() );
    verticalScrollBar()->setValue( verticalScrollBar()->value() - delta.y() );
    event->accept();
    return;
  }
  QGraphicsView::mouseMoveEvent( event );
}

void PipelineCanvasWidget::mouseReleaseEvent( QMouseEvent *event )
{
  if ( mIsPanning && ( event->button() == Qt::MiddleButton || event->button() == Qt::LeftButton ) )
  {
    mIsPanning = false;
    setCursor( Qt::ArrowCursor );
    event->accept();
    return;
  }
  QGraphicsView::mouseReleaseEvent( event );
}

void PipelineCanvasWidget::contextMenuEvent( QContextMenuEvent *event )
{
  QPointF scenePos = mapToScene( event->pos() );
  QGraphicsItem *item = mScene->itemAt( scenePos, QTransform() );

  PipelineNodeItem *node = dynamic_cast<PipelineNodeItem *>( item );
  if ( !node && item && item->parentItem() )
  {
    node = dynamic_cast<PipelineNodeItem *>( item->parentItem() );
  }

  QMenu menu( this );
  if ( node )
  {
    QString stepId = node->stepId();
    QAction *runUpToAct = menu.addAction( QString( "▶ Run Up to '%1'" ).arg( node->title() ) );
    QAction *viewLogsAct = menu.addAction( "📜 View Live Execution Logs" );
    menu.addSeparator();
    QAction *deleteNodeAct = menu.addAction( "🗑️ Delete Node" );

    QAction *selectedAct = menu.exec( event->globalPos() );
    if ( selectedAct == runUpToAct )
    {
      emit runUpToNodeRequested( stepId );
    }
    else if ( selectedAct == viewLogsAct )
    {
      emit viewNodeLogsRequested( stepId );
    }
    else if ( selectedAct == deleteNodeAct )
    {
      mScene->removeNode( stepId );
    }
  }
  else
  {
    QAction *zoomFitAct = menu.addAction( "🔍 Zoom to Fit" );
    QAction *resetZoomAct = menu.addAction( "🔄 Reset Zoom" );

    QAction *selectedAct = menu.exec( event->globalPos() );
    if ( selectedAct == zoomFitAct )
    {
      zoomToFit();
    }
    else if ( selectedAct == resetZoomAct )
    {
      resetZoom();
    }
  }
}

} // namespace sicnu::workflow::gui
