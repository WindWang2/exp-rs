// rs_roi_tool_freehand.cpp — see header for design notes.
#include "rs_roi_tool_freehand.h"

#include "qgsmapmouseevent.h"

void RsRoiToolFreehand::canvasPressEvent( QgsMapMouseEvent *e )
{
  if ( !e )
    return;
  mDragging = true;
  mPath.clear();
  mPath.append( toMapCoordinates( e->pos() ) );
}

void RsRoiToolFreehand::canvasMoveEvent( QgsMapMouseEvent *e )
{
  if ( !e || !mDragging )
    return;
  mPath.append( toMapCoordinates( e->pos() ) );
}

void RsRoiToolFreehand::canvasReleaseEvent( QgsMapMouseEvent * /*e*/ )
{
  if ( !mDragging )
    return;
  mDragging = false;
  if ( mPath.size() < 3 )
  {
    mPath.clear();
    return;
  }
  QVector<QgsPointXY> ring = mPath;
  ring.append( mPath.first() ); // close
  emit roiDrawn( QgsGeometry::fromPolygonXY( QgsPolygonXY{ ring } ), mClassId );
  mPath.clear();
}
