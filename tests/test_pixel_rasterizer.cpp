// test_pixel_rasterizer.cpp — Phase 10A Task 10.4
//
// Verifies that RsPixelRasterizer correctly converts a QgsGeometry to a set
// of pixel indices (row * W + col) using a GDAL in-memory raster.
#include "rs_pixel_rasterizer.h"
#include "qgsgeometry.h"

#include <catch2/catch_test_macros.hpp>

TEST_CASE( "Rasterizer: 10x10 polygon yields ~100 pixels", "[classify][rasterize]" )
{
    // 100x100 raster. GeoTransform places origin at (0, 100) top-left, with
    // pixel size 1.0 in X and -1.0 in Y (so Y axis points downward).
    // Polygon covering map coords (0,0) -> (10,10) covers the bottom-left
    // 10x10 region: pixel rows 90..99, cols 0..9.
    double gt[6] = { 0, 1, 0, 100, 0, -1 };
    QgsGeometry geom = QgsGeometry::fromWkt(
        QStringLiteral( "POLYGON((0 0, 10 0, 10 10, 0 10, 0 0))" ) );
    auto indices = RsPixelRasterizer::rasterize( geom, gt, 100, 100 );

    // GDAL edge-inclusion rules vary slightly; allow 81..121 tolerance.
    REQUIRE( indices.size() >= 81 );
    REQUIRE( indices.size() <= 121 );
    // The pixel at row 90, col 0 (top-left corner of the burned region in
    // pixel space) should be present in the result.
    REQUIRE( indices.contains( quint64( 90 ) * 100 + 0 ) );
}

TEST_CASE( "Rasterizer: out-of-bounds polygon yields empty set", "[classify][rasterize]" )
{
    double gt[6] = { 0, 1, 0, 100, 0, -1 };
    QgsGeometry geom = QgsGeometry::fromWkt(
        QStringLiteral( "POLYGON((200 200, 210 200, 210 210, 200 210, 200 200))" ) );
    auto indices = RsPixelRasterizer::rasterize( geom, gt, 100, 100 );
    REQUIRE( indices.size() == 0 );
}
