// rs_roi_tool_polygon.cpp — see header for design notes.
#include "rs_roi_tool_polygon.h"

#include "qgsmapmouseevent.h"

void RsRoiToolPolygon::canvasReleaseEvent( QgsMapMouseEvent *e )
{
  if ( !e )
    return;
  mVertices.append( toMapCoordinates( e->pos() ) );
}

void RsRoiToolPolygon::canvasDoubleClickEvent( QgsMapMouseEvent * /*e*/ )
{
  if ( mVertices.size() < 3 )
  {
    mVertices.clear();
    return;
  }
  // Close the ring: append the first vertex at the end to form a valid
  // polygon according to OGR / Simple Features.
  QVector<QgsPointXY> ring = mVertices;
  ring.append( mVertices.first() );
  emit roiDrawn( QgsGeometry::fromPolygonXY( QgsPolygonXY{ ring } ), mClassId );
  mVertices.clear();
}
