// test_pixel_rasterizer.cpp — Phase 10A Task 10.4
//
// Verifies that RsPixelRasterizer correctly converts a QgsGeometry to a set
// of pixel indices (row * W + col) using a GDAL in-memory raster, and that
// the fail-closed contract of #1056 holds: infrastructure failures report
// ok=false while genuinely empty coverage reports ok=true with an empty set.
#include "rs_pixel_rasterizer.h"
#include "qgsgeometry.h"

#include <catch2/catch_test_macros.hpp>

namespace
{

/// RAII guard for the process-global rasterizer failure-injection hooks:
/// restores the null (production) state no matter how the test exits.
class FailureHookGuard
{
  public:
    ~FailureHookGuard()
    {
      RsPixelRasterizer::sForceRasterizeGeometriesFailure = nullptr;
      RsPixelRasterizer::sForceRasterIOFailure = nullptr;
    }
};

} // namespace

TEST_CASE( "Rasterizer: 10x10 polygon yields ~100 pixels", "[classify][rasterize]" )
{
    // 100x100 raster. GeoTransform places origin at (0, 100) top-left, with
    // pixel size 1.0 in X and -1.0 in Y (so Y axis points downward).
    // Polygon covering map coords (0,0) -> (10,10) covers the bottom-left
    // 10x10 region: pixel rows 90..99, cols 0..9.
    double gt[6] = { 0, 1, 0, 100, 0, -1 };
    QgsGeometry geom = QgsGeometry::fromWkt(
        QStringLiteral( "POLYGON((0 0, 10 0, 10 10, 0 10, 0 0))" ) );
    bool ok = false;
    auto indices = RsPixelRasterizer::rasterize( geom, gt, 100, 100, &ok );

    // GDAL edge-inclusion rules vary slightly; allow 81..121 tolerance.
    REQUIRE( indices.size() >= 81 );
    REQUIRE( indices.size() <= 121 );
    // The pixel at row 90, col 0 (top-left corner of the burned region in
    // pixel space) should be present in the result.
    REQUIRE( indices.contains( quint64( 90 ) * 100 + 0 ) );
    // Happy path is exact coverage, reported as such (#1056).
    REQUIRE( ok );
}

TEST_CASE( "Rasterizer: out-of-bounds polygon yields empty set", "[classify][rasterize]" )
{
    double gt[6] = { 0, 1, 0, 100, 0, -1 };
    QgsGeometry geom = QgsGeometry::fromWkt(
        QStringLiteral( "POLYGON((200 200, 210 200, 210 210, 200 210, 200 200))" ) );
    bool ok = false;
    auto indices = RsPixelRasterizer::rasterize( geom, gt, 100, 100, &ok );
    REQUIRE( indices.size() == 0 );
    // Genuinely empty coverage — not an error (#1056).
    REQUIRE( ok );
}

TEST_CASE( "Rasterizer: null geometry is empty coverage, not failure", "[classify][rasterize]" )
{
    double gt[6] = { 0, 1, 0, 100, 0, -1 };
    QgsGeometry geom; // null
    bool ok = false;
    auto indices = RsPixelRasterizer::rasterize( geom, gt, 100, 100, &ok );
    REQUIRE( indices.isEmpty() );
    REQUIRE( ok );
}

TEST_CASE( "Rasterizer: non-invertible geotransform fails closed", "[classify][rasterize]" )
{
    // All-zero GT cannot map between map and pixel space — coverage is
    // unknowable and must read as failure, not "no pixels" (#1056).
    double gt[6] = { 0, 0, 0, 0, 0, 0 };
    QgsGeometry geom = QgsGeometry::fromWkt(
        QStringLiteral( "POLYGON((0 0, 10 0, 10 10, 0 10, 0 0))" ) );
    bool ok = true;
    auto indices = RsPixelRasterizer::rasterize( geom, gt, 100, 100, &ok );
    REQUIRE( indices.isEmpty() );
    REQUIRE( !ok );
}

TEST_CASE( "Rasterizer: forced GDALRasterizeGeometries failure fails closed", "[classify][rasterize]" )
{
    FailureHookGuard guard;
    double gt[6] = { 0, 1, 0, 100, 0, -1 };
    QgsGeometry geom = QgsGeometry::fromWkt(
        QStringLiteral( "POLYGON((0 0, 10 0, 10 10, 0 10, 0 0))" ) );

    RsPixelRasterizer::sForceRasterizeGeometriesFailure = []() { return true; };

    bool ok = true;
    auto indices = RsPixelRasterizer::rasterize( geom, gt, 100, 100, &ok );
    REQUIRE( indices.isEmpty() );
    REQUIRE( !ok );

    // A failed rasterization must never masquerade as empty coverage for
    // legacy callers either (both collapse to the empty set).
    auto legacy = RsPixelRasterizer::rasterize( geom, gt, 100, 100 );
    REQUIRE( legacy.isEmpty() );
}

TEST_CASE( "Rasterizer: forced scanline RasterIO failure fails closed", "[classify][rasterize]" )
{
    FailureHookGuard guard;
    double gt[6] = { 0, 1, 0, 100, 0, -1 };
    QgsGeometry geom = QgsGeometry::fromWkt(
        QStringLiteral( "POLYGON((0 0, 10 0, 10 10, 0 10, 0 0))" ) );

    RsPixelRasterizer::sForceRasterIOFailure = []() { return true; };

    bool ok = true;
    auto indices = RsPixelRasterizer::rasterize( geom, gt, 100, 100, &ok );
    REQUIRE( indices.isEmpty() );
    REQUIRE( !ok );
}
