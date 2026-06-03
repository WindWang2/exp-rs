// rs_roi_tool_point.cpp — see header for design notes.
#include "rs_roi_tool_point.h"

#include "qgsmapmouseevent.h"
#include "qgspointxy.h"

void RsRoiToolPoint::canvasReleaseEvent( QgsMapMouseEvent *e )
{
  if ( !e )
    return;
  const QgsPointXY pt = toMapCoordinates( e->pos() );
  emit roiDrawn( QgsGeometry::fromPointXY( pt ), mClassId );
}
