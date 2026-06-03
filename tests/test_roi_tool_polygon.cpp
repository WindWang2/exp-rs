// test_roi_tool_polygon.cpp — Phase 10A Task 10.4
//
// Verifies the polygon ROI map tool: three single-clicks (release events)
// followed by a double-click emit a single roiDrawn(QgsGeometry, classId)
// signal carrying a Polygon geometry with the configured class id.
#include "qgsmapcanvas.h"
#include "qgsmapmouseevent.h"
#include "qgsrectangle.h"
#include "rs_roi_tool_polygon.h"

#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QMouseEvent>
#include <QPointF>
#include <QSignalSpy>

namespace
{
QApplication *ensureApp()
{
  static int argc = 1;
  static char arg0[] = "test";
  static char *argv[] = { arg0, nullptr };
  return qApp ? nullptr : new QApplication( argc, argv );
}
} // namespace

TEST_CASE( "RoiToolPolygon: 3 clicks + double-click emits triangle geometry",
           "[classify][roitool]" )
{
  ensureApp();
  QgsMapCanvas canvas;
  canvas.resize( 500, 500 );
  canvas.setExtent( QgsRectangle( 0, 0, 100, 100 ) );

  RsRoiToolPolygon tool( &canvas );
  tool.setCurrentClassId( 3 );
  QSignalSpy spy( &tool, &RsRoiToolBase::roiDrawn );

  auto fire = [&]( int x, int y, QEvent::Type type )
  {
    QMouseEvent me( type,
                    QPointF( x, y ),
                    Qt::LeftButton,
                    Qt::LeftButton,
                    Qt::NoModifier );
    QgsMapMouseEvent mme( &canvas, &me );
    if ( type == QEvent::MouseButtonRelease )
      tool.canvasReleaseEvent( &mme );
    else if ( type == QEvent::MouseButtonDblClick )
      tool.canvasDoubleClickEvent( &mme );
  };

  fire( 50, 50, QEvent::MouseButtonRelease );
  fire( 150, 50, QEvent::MouseButtonRelease );
  fire( 100, 150, QEvent::MouseButtonRelease );
  fire( 100, 100, QEvent::MouseButtonDblClick );

  REQUIRE( spy.count() == 1 );
  const auto args = spy.takeFirst();
  const auto geom = args.at( 0 ).value<QgsGeometry>();
  REQUIRE_FALSE( geom.isNull() );
  REQUIRE( geom.type() == Qgis::GeometryType::Polygon );
  REQUIRE( args.at( 1 ).toInt() == 3 );
}
