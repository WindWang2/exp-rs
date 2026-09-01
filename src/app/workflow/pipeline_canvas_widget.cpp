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
  setViewportUpdateMode( QGraphicsView::SmartViewportUpdate );

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

void PipelineCanvasWidget::deleteSelected()
{
  auto selected = mScene->selectedItems();
  std::vector<PipelineConnectionItem *> connsToDelete;
  std::vector<PipelineNodeItem *> nodesToDelete;

  for ( auto *item : selected )
  {
    if ( auto *conn = dynamic_cast<PipelineConnectionItem *>( item ) )
    {
      connsToDelete.push_back( conn );
    }
    else if ( auto *node = dynamic_cast<PipelineNodeItem *>( item ) )
    {
      nodesToDelete.push_back( node );
    }
  }

  for ( auto *conn : connsToDelete )
  {
    mScene->removeConnection( conn );
  }

  for ( auto *node : nodesToDelete )
  {
    mScene->removeNode( node->stepId() );
  }
}

void PipelineCanvasWidget::keyPressEvent( QKeyEvent *event )
{
  if ( event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace )
  {
    deleteSelected();
    event->accept();
    return;
  }
  if ( ( event->modifiers() & Qt::ControlModifier ) && event->key() == Qt::Key_A )
  {
    for ( auto *item : mScene->items() )
    {
      item->setSelected( true );
    }
    event->accept();
    return;
  }
  if ( event->key() == Qt::Key_Escape )
  {
    mScene->cancelTempConnection();
    mScene->clearSelection();
    event->accept();
    return;
  }

  QGraphicsView::keyPressEvent( event );
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

  PipelineConnectionItem *conn = dynamic_cast<PipelineConnectionItem *>( item );

  QMenu menu( this );
  if ( node )
  {
    QString stepId = node->stepId();
    QAction *runUpToAct = menu.addAction( QIcon::fromTheme( QStringLiteral( "media-playback-start" ), QIcon( QStringLiteral( ":/icons/media-playback-start" ) ) ), tr( "执行至此节点 '%1'" ).arg( node->title() ) );
    QAction *viewLogsAct = menu.addAction( QIcon::fromTheme( QStringLiteral( "text-x-generic" ), QIcon( QStringLiteral( ":/icons/text-x-generic" ) ) ), tr( "查看实时执行日志" ) );
    menu.addSeparator();
    QAction *deleteNodeAct = menu.addAction( QIcon::fromTheme( QStringLiteral( "edit-delete" ), QIcon( QStringLiteral( ":/icons/edit-delete" ) ) ), tr( "删除节点" ) );

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
  else if ( conn )
  {
    QAction *deleteConnAct = menu.addAction( QIcon::fromTheme( QStringLiteral( "edit-delete" ), QIcon( QStringLiteral( ":/icons/edit-delete" ) ) ), tr( "删除连线" ) );
    QAction *selectedAct = menu.exec( event->globalPos() );
    if ( selectedAct == deleteConnAct )
    {
      mScene->removeConnection( conn );
    }
  }
  else
  {
    QAction *zoomFitAct = menu.addAction( QIcon::fromTheme( QStringLiteral( "zoom-fit-best" ), QIcon( QStringLiteral( ":/icons/zoom-fit-best" ) ) ), tr( "适应窗口" ) );
    QAction *resetZoomAct = menu.addAction( QIcon::fromTheme( QStringLiteral( "zoom-original" ), QIcon( QStringLiteral( ":/icons/zoom-original" ) ) ), tr( "100% 视图" ) );

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
