// tests/test_roi_io.cpp — Phase 10A Task 10.1: RsRoiIO shapefile + sidecar JSON round-trip
#include <catch2/catch_test_macros.hpp>
#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>

#include "rs_roi_io.h"
#include "rs_roi_collection.h"
#include "rs_class_def.h"
#include "qgsgeometry.h"

TEST_CASE( "RoiIO: write+read shapefile preserves class id and sidecar JSON", "[classify][roi][io]" )
{
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );
  const QString path = tmp.path() + QStringLiteral( "/rois.shp" );

  RsRoiCollection orig;
  orig.setClassDef( RsClassDef( 1, QStringLiteral( "Forest" ), QColor( "#2da44e" ) ) );
  orig.setClassDef( RsClassDef( 2, QStringLiteral( "Water" ), QColor( "#0969da" ) ) );
  // The FRESH RsRoi objects built here carry pixel indices; the LOADED ROIs
  // will have empty pixelIndices() because raster-coordinate–dependent pixel
  // sets are not persisted by the shapefile (see RsRoiIO::save header comment).
  orig.appendRoi( RsRoi( 1,
                         QgsGeometry::fromWkt( QStringLiteral( "POLYGON((0 0,10 0,10 10,0 10,0 0))" ) ),
                         QVector<quint64>{ 1, 2, 3, 4 } ) );
  orig.appendRoi( RsRoi( 2,
                         QgsGeometry::fromWkt( QStringLiteral( "POLYGON((20 20,30 20,30 30,20 30,20 20))" ) ),
                         QVector<quint64>{ 10, 11 } ) );

  REQUIRE( RsRoiIO::save( path, orig ) );
  REQUIRE( QFile::exists( path ) );
  REQUIRE( QFile::exists( tmp.path() + QStringLiteral( "/rois.classes.json" ) ) );

  RsRoiCollection loaded;
  REQUIRE( RsRoiIO::load( path, loaded ) );
  REQUIRE( loaded.size() == 2 );
  REQUIRE( loaded.at( 0 ).classId() == 1 );
  REQUIRE( loaded.at( 1 ).classId() == 2 );
  // Geometry preserved.
  REQUIRE_FALSE( loaded.at( 0 ).geometry().isNull() );
  REQUIRE_FALSE( loaded.at( 1 ).geometry().isNull() );
  // Pixel indices are intentionally NOT round-tripped — caller recomputes.
  REQUIRE( loaded.at( 0 ).pixelIndices().isEmpty() );
  REQUIRE( loaded.at( 1 ).pixelIndices().isEmpty() );

  // Sidecar JSON brought class defs back.
  REQUIRE( loaded.classDefs().size() == 2 );
  REQUIRE( loaded.classDef( 1 ).name() == QStringLiteral( "Forest" ) );
  REQUIRE( loaded.classDef( 2 ).color() == QColor( "#0969da" ) );
}

TEST_CASE( "RoiIO: load returns false on invalid path", "[classify][roi][io]" )
{
  RsRoiCollection col;
  REQUIRE_FALSE( RsRoiIO::load( QStringLiteral( "/does/not/exist.shp" ), col ) );
  REQUIRE( col.size() == 0 );
}
