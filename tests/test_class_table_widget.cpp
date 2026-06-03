// Phase 10A Task 10.3 — RsClassTableWidget tests.
#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QSignalSpy>

#include "rs_class_table_widget.h"
#include "rs_roi_collection.h"
#include "qgsgeometry.h"

namespace
{
  QApplication *ensureApp()
  {
    static int argc = 1;
    static char a[] = "t";
    static char *v[] = { a, nullptr };
    return qApp ? nullptr : new QApplication( argc, v );
  }
} // namespace

TEST_CASE( "ClassTable: displays 6 classes with ROI counts", "[classify][table]" )
{
  ensureApp();
  RsRoiCollection col;
  col.setClassDef( RsClassDef( 1, "Forest", QColor( "#2da44e" ) ) );
  col.setClassDef( RsClassDef( 2, "Water", QColor( "#0969da" ) ) );
  auto g = QgsGeometry::fromWkt( "POINT(1 1)" );
  col.appendRoi( RsRoi( 1, g, { 10, 11, 12 } ) );
  col.appendRoi( RsRoi( 1, g, { 20, 21 } ) );
  col.appendRoi( RsRoi( 2, g, { 30 } ) );

  RsClassTableWidget w;
  w.setRoiCollection( &col );
  REQUIRE( w.rowCount() == 2 );
  REQUIRE( w.roiCountForRow( 0 ) == 2 );
  REQUIRE( w.pixelCountForRow( 0 ) == 5 );
  REQUIRE( w.roiCountForRow( 1 ) == 1 );
  REQUIRE( w.pixelCountForRow( 1 ) == 1 );
}

TEST_CASE( "ClassTable: currentClassChanged on row selection", "[classify][table]" )
{
  ensureApp();
  RsRoiCollection col;
  col.setClassDef( RsClassDef( 1, "A", QColor( "#0a0" ) ) );
  col.setClassDef( RsClassDef( 2, "B", QColor( "#a00" ) ) );

  RsClassTableWidget w;
  w.setRoiCollection( &col );
  QSignalSpy spy( &w, &RsClassTableWidget::currentClassChanged );
  w.setCurrentRow( 1 );
  REQUIRE( spy.count() >= 1 );
  REQUIRE( w.currentClassId() == 2 );
}
