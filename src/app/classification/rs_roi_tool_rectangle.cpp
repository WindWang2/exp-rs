// rs_roi_tool_rectangle.cpp — see header for design notes.
#include "rs_roi_tool_rectangle.h"

#include "qgsmapcanvas.h"
#include "qgsmapmouseevent.h"
#include "qgsrubberband.h"
#include "qgis.h"

#include <QColor>

RsRoiToolRectangle::RsRoiToolRectangle( QgsMapCanvas *canvas )
  : RsRoiToolBase( canvas )
{
  mRubber = new QgsRubberBand( canvas, Qgis::GeometryType::Polygon );
  mRubber->setStrokeColor( QColor( 255, 200, 0 ) );
  mRubber->setFillColor( QColor( 255, 200, 0, 60 ) );
  mRubber->setWidth( 2 );
  mRubber->hide();
}

RsRoiToolRectangle::~RsRoiToolRectangle()
{
  delete mRubber;
  mRubber = nullptr;
}

void RsRoiToolRectangle::clearRubber()
{
  if ( mRubber )
  {
    mRubber->reset( Qgis::GeometryType::Polygon );
    mRubber->hide();
  }
}

void RsRoiToolRectangle::deactivate()
{
  mHasPress = false;
  clearRubber();
  QgsMapTool::deactivate();
}

void RsRoiToolRectangle::canvasPressEvent( QgsMapMouseEvent *e )
{
  if ( !e )
    return;
  mPressed = toMapCoordinates( e->pos() );
  mHasPress = true;
  if ( mRubber )
  {
    mRubber->reset( Qgis::GeometryType::Polygon );
    mRubber->show();
  }
}

void RsRoiToolRectangle::canvasMoveEvent( QgsMapMouseEvent *e )
{
  if ( !e || !mHasPress || !mRubber )
    return;
  const QgsPointXY cur = toMapCoordinates( e->pos() );
  QVector<QgsPointXY> ring;
  ring.reserve( 5 );
  ring << mPressed
       << QgsPointXY( cur.x(), mPressed.y() )
       << cur
       << QgsPointXY( mPressed.x(), cur.y() )
       << mPressed;
  mRubber->setToGeometry( QgsGeometry::fromPolygonXY( QgsPolygonXY{ ring } ),
                          QgsCoordinateReferenceSystem() );
}

void RsRoiToolRectangle::canvasReleaseEvent( QgsMapMouseEvent *e )
{
  if ( !e || !mHasPress )
    return;
  mHasPress = false;
  const QgsPointXY rel = toMapCoordinates( e->pos() );
  clearRubber();

  // Ignore zero-area clicks (no drag).
  if ( qgsDoubleNear( rel.x(), mPressed.x() ) && qgsDoubleNear( rel.y(), mPressed.y() ) )
    return;

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
