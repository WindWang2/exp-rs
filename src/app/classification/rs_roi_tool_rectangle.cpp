// rs_roi_tool_rectangle.cpp — see header for design notes.
#include "rs_roi_tool_rectangle.h"

#include "qgsmapmouseevent.h"

void RsRoiToolRectangle::canvasPressEvent( QgsMapMouseEvent *e )
{
  if ( !e )
    return;
  mPressed = toMapCoordinates( e->pos() );
  mHasPress = true;
}

void RsRoiToolRectangle::canvasReleaseEvent( QgsMapMouseEvent *e )
{
  if ( !e || !mHasPress )
    return;
  mHasPress = false;
  const QgsPointXY rel = toMapCoordinates( e->pos() );

  // Build a closed CCW ring from the two opposite corners.
  QVector<QgsPointXY> ring;
  ring.reserve( 5 );
  ring << mPressed
       << QgsPointXY( rel.x(), mPressed.y() )
       << rel
       << QgsPointXY( mPressed.x(), rel.y() )
       << mPressed; // close
  emit roiDrawn( QgsGeometry::fromPolygonXY( QgsPolygonXY{ ring } ), mClassId );
}
