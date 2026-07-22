// rs_roi_tool_polygon.cpp — see header for design notes.
#include "rs_roi_tool_polygon.h"

#include "qgsmapcanvas.h"
#include "qgsmapmouseevent.h"
#include "qgsrubberband.h"
#include "qgis.h"

#include <QColor>

RsRoiToolPolygon::RsRoiToolPolygon( QgsMapCanvas *canvas )
  : RsRoiToolBase( canvas )
{
  mRubber = new QgsRubberBand( canvas, Qgis::GeometryType::Polygon );
  mRubber->setStrokeColor( QColor( 255, 200, 0 ) );
  mRubber->setFillColor( QColor( 255, 200, 0, 60 ) );
  mRubber->setWidth( 2 );
  mRubber->hide();
}

RsRoiToolPolygon::~RsRoiToolPolygon()
{
  delete mRubber;
  mRubber = nullptr;
}

void RsRoiToolPolygon::clearRubber()
{
  if ( mRubber )
  {
    mRubber->reset( Qgis::GeometryType::Polygon );
    mRubber->hide();
  }
}

void RsRoiToolPolygon::updateRubber( const QgsPointXY *cursor )
{
  if ( !mRubber || mVertices.isEmpty() )
    return;
  QVector<QgsPointXY> ring = mVertices;
  if ( cursor )
    ring.append( *cursor );
  if ( ring.size() >= 2 )
  {
    // Close visually back to first vertex for preview.
    ring.append( mVertices.first() );
    mRubber->setToGeometry( QgsGeometry::fromPolygonXY( QgsPolygonXY{ ring } ),
                            QgsCoordinateReferenceSystem() );
    mRubber->show();
  }
}

void RsRoiToolPolygon::deactivate()
{
  mVertices.clear();
  clearRubber();
  QgsMapTool::deactivate();
}

void RsRoiToolPolygon::canvasReleaseEvent( QgsMapMouseEvent *e )
{
  if ( !e )
    return;
  mVertices.append( toMapCoordinates( e->pos() ) );
  updateRubber();
}

void RsRoiToolPolygon::canvasMoveEvent( QgsMapMouseEvent *e )
{
  if ( !e || mVertices.isEmpty() )
    return;
  const QgsPointXY cur = toMapCoordinates( e->pos() );
  updateRubber( &cur );
}

void RsRoiToolPolygon::canvasDoubleClickEvent( QgsMapMouseEvent * /*e*/ )
{
  if ( mVertices.size() < 3 )
  {
    mVertices.clear();
    clearRubber();
    return;
  }
  // Close the ring: append the first vertex at the end to form a valid
  // polygon according to OGR / Simple Features.
  QVector<QgsPointXY> ring = mVertices;
  ring.append( mVertices.first() );
  clearRubber();
  emit roiDrawn( QgsGeometry::fromPolygonXY( QgsPolygonXY{ ring } ), mClassId );
  mVertices.clear();
}
