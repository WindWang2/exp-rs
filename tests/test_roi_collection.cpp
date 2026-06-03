// tests/test_roi_collection.cpp — Phase 10A Task 10.1: RsRoiCollection signals + filters
#include <catch2/catch_test_macros.hpp>
#include <QSignalSpy>

#include "rs_roi_collection.h"
#include "rs_class_def.h"
#include "qgsgeometry.h"

TEST_CASE( "RoiCollection: appendRoi emits roiAdded and changed", "[classify][roi]" )
{
  RsRoiCollection col;
  QSignalSpy added( &col, &RsRoiCollection::roiAdded );
  QSignalSpy changed( &col, &RsRoiCollection::changed );

  QgsGeometry geom = QgsGeometry::fromWkt( QStringLiteral( "POLYGON((0 0, 10 0, 10 10, 0 10, 0 0))" ) );
  col.appendRoi( RsRoi( 1, geom, QVector<quint64>{ 0, 1, 2, 3 } ) );

  REQUIRE( added.count() == 1 );
  REQUIRE( changed.count() == 1 );
  REQUIRE( col.size() == 1 );
  REQUIRE( col.at( 0 ).classId() == 1 );
  REQUIRE( col.at( 0 ).pixelIndices().size() == 4 );
}

TEST_CASE( "RoiCollection: filter by classId", "[classify][roi]" )
{
  RsRoiCollection col;
  QgsGeometry g = QgsGeometry::fromWkt( QStringLiteral( "POINT(1 1)" ) );
  col.appendRoi( RsRoi( 1, g, QVector<quint64>{ 10 } ) );
  col.appendRoi( RsRoi( 2, g, QVector<quint64>{ 20 } ) );
  col.appendRoi( RsRoi( 1, g, QVector<quint64>{ 30 } ) );

  QVector<RsRoi> cls1 = col.roisForClass( 1 );
  REQUIRE( cls1.size() == 2 );

  REQUIRE( col.pixelCountForClass( 1 ) == 2u );
  REQUIRE( col.pixelCountForClass( 2 ) == 1u );
  REQUIRE( col.pixelCountForClass( 99 ) == 0u );
}

TEST_CASE( "RoiCollection: class definitions independent of ROIs", "[classify][roi]" )
{
  RsRoiCollection col;
  QSignalSpy defChanged( &col, &RsRoiCollection::classDefChanged );

  col.setClassDef( RsClassDef( 1, QStringLiteral( "Forest" ), QColor( "#2da44e" ) ) );
  col.setClassDef( RsClassDef( 2, QStringLiteral( "Water" ), QColor( "#0969da" ) ) );

  REQUIRE( defChanged.count() == 2 );
  REQUIRE( col.classDefs().size() == 2 );
  REQUIRE( col.classDef( 1 ).name() == QStringLiteral( "Forest" ) );
  REQUIRE( col.classDef( 2 ).color() == QColor( "#0969da" ) );

  // No ROIs created — class table can exist without any ROIs
  REQUIRE( col.size() == 0 );
}
