// rs_roi_tool_freehand.cpp — see header for design notes.
#include "rs_roi_tool_freehand.h"

#include "qgsmapcanvas.h"
#include "qgsmapmouseevent.h"
#include "qgsrubberband.h"
#include "qgis.h"

#include <QColor>

RsRoiToolFreehand::RsRoiToolFreehand( QgsMapCanvas *canvas )
  : RsRoiToolBase( canvas )
{
  mRubber = new QgsRubberBand( canvas, Qgis::GeometryType::Polygon );
  mRubber->setStrokeColor( QColor( 255, 200, 0 ) );
  mRubber->setFillColor( QColor( 255, 200, 0, 60 ) );
  mRubber->setWidth( 2 );
  mRubber->hide();
}

RsRoiToolFreehand::~RsRoiToolFreehand()
{
  delete mRubber;
  mRubber = nullptr;
}

void RsRoiToolFreehand::clearRubber()
{
  if ( mRubber )
  {
    mRubber->reset( Qgis::GeometryType::Polygon );
    mRubber->hide();
  }
}

void RsRoiToolFreehand::deactivate()
{
  mDragging = false;
  mPath.clear();
  clearRubber();
  QgsMapTool::deactivate();
}

void RsRoiToolFreehand::canvasPressEvent( QgsMapMouseEvent *e )
{
  if ( !e )
    return;
  mDragging = true;
  mPath.clear();
  mPath.append( toMapCoordinates( e->pos() ) );
  if ( mRubber )
  {
    mRubber->reset( Qgis::GeometryType::Polygon );
    mRubber->show();
  }
}

void RsRoiToolFreehand::canvasMoveEvent( QgsMapMouseEvent *e )
{
  if ( !e || !mDragging )
    return;
  mPath.append( toMapCoordinates( e->pos() ) );
  if ( mRubber && mPath.size() >= 2 )
  {
    QVector<QgsPointXY> ring = mPath;
    ring.append( mPath.first() );
    mRubber->setToGeometry( QgsGeometry::fromPolygonXY( QgsPolygonXY{ ring } ),
                            QgsCoordinateReferenceSystem() );
  }
}

void RsRoiToolFreehand::canvasReleaseEvent( QgsMapMouseEvent * /*e*/ )
{
  if ( !mDragging )
    return;
  mDragging = false;
  clearRubber();
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
