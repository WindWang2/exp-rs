// test_raster_grid_compat.cpp — shared raster-grid compatibility service
#include <catch2/catch_test_macros.hpp>

#include <QString>
#include <QVector>

#include <array>
#include <optional>

#include "data/raster_grid_compat.h"

using sicnu::data::GridCompatIssue;
using sicnu::data::GridCompatReport;
using sicnu::data::GridCompatVerdict;
using sicnu::data::RasterBandStructure;
using sicnu::data::RasterGrid;
using sicnu::data::RasterStructure;

namespace
{

/// 30 m north-up grid in "EPSG:32648", origin (500000, 4500000).
RasterGrid makeGrid( int width = 100, int height = 100, double originX = 500000.0,
                     double originY = 4500000.0, double pixelX = 30.0,
                     double pixelY = 30.0 )
{
  RasterGrid grid;
  grid.crsWkt = QStringLiteral( "EPSG:32648" );
  grid.hasGeoTransform = true;
  grid.geoTransform = { originX, pixelX, 0.0, originY, 0.0, -pixelY };
  grid.width = width;
  grid.height = height;
  grid.bandNoData = { std::optional<double>( -9999.0 ) };
  return grid;
}

const GridCompatIssue *firstIssue( const GridCompatReport &report, GridCompatVerdict verdict )
{
  for ( const GridCompatIssue &issue : report.issues )
  {
    if ( issue.verdict == verdict )
      return &issue;
  }
  return nullptr;
}

} // namespace

TEST_CASE( "Identical grids are compatible", "[grid_compat]" )
{
  const RasterGrid a = makeGrid();
  const GridCompatReport report = sicnu::data::compareGrids( a, a );
  CHECK( report.compatible() );
  CHECK( report.aligned() );
  CHECK( report.issues.isEmpty() );
}

TEST_CASE( "Differing CRSs need reprojection", "[grid_compat]" )
{
  RasterGrid a = makeGrid();
  RasterGrid b = makeGrid();
  b.crsWkt = QStringLiteral( "EPSG:4326" );

  const GridCompatReport report = sicnu::data::compareGrids( a, b );
  CHECK_FALSE( report.compatible() );
  const auto primary = report.primaryBlocking();
  REQUIRE( primary.has_value() );
  CHECK( primary->verdict == GridCompatVerdict::CrsMismatch );
  CHECK( primary->code == QStringLiteral( "grid.crs_mismatch" ) );
  CHECK( primary->message.contains( QStringLiteral( "reproject" ) ) );
}

TEST_CASE( "A single unreferenced raster is missing a CRS", "[grid_compat]" )
{
  const RasterGrid a = makeGrid();
  RasterGrid b = makeGrid();
  b.crsWkt.clear();

  const GridCompatReport report = sicnu::data::compareGrids( a, b );
  CHECK_FALSE( report.compatible() );
  REQUIRE( firstIssue( report, GridCompatVerdict::MissingCrs ) != nullptr );
}

TEST_CASE( "Two unreferenced rasters pass as compatible (caller falls back to dims)",
           "[grid_compat]" )
{
  RasterGrid a = makeGrid();
  a.crsWkt.clear();
  a.hasGeoTransform = false;
  a.geoTransform = { 0, 1, 0, 0, 0, -1 };
  RasterGrid b = a;

  const GridCompatReport report = sicnu::data::compareGrids( a, b );
  CHECK( report.compatible() );
  CHECK( report.aligned() );
}

TEST_CASE( "Differing pixel sizes need resampling", "[grid_compat]" )
{
  const RasterGrid a = makeGrid();
  const RasterGrid b = makeGrid( 50, 50, 500000.0, 4500000.0, 60.0, 60.0 );

  const GridCompatReport report = sicnu::data::compareGrids( a, b );
  CHECK_FALSE( report.compatible() );
  const auto primary = report.primaryBlocking();
  REQUIRE( primary.has_value() );
  CHECK( primary->verdict == GridCompatVerdict::PixelSizeMismatch );
  CHECK( primary->code == QStringLiteral( "grid.pixel_size_mismatch" ) );
  CHECK( primary->message.contains( QStringLiteral( "30" ) ) );
  CHECK( primary->message.contains( QStringLiteral( "60" ) ) );
  CHECK( primary->message.contains( QStringLiteral( "resample" ) ) );
}

TEST_CASE( "A rotated grid is a pixel-grid mismatch", "[grid_compat]" )
{
  RasterGrid b = makeGrid();
  b.geoTransform[2] = 0.001;

  const GridCompatReport report = sicnu::data::compareGrids( makeGrid(), b );
  CHECK_FALSE( report.compatible() );
  REQUIRE( firstIssue( report, GridCompatVerdict::PixelSizeMismatch ) != nullptr );
}

TEST_CASE( "A sub-pixel origin offset is a misalignment", "[grid_compat]" )
{
  // Same lattice, but the second grid's origin is 15 m (half a 30 m pixel) off.
  const RasterGrid b = makeGrid( 100, 100, 500015.0, 4500000.0 );

  const GridCompatReport report = sicnu::data::compareGrids( makeGrid(), b );
  CHECK_FALSE( report.compatible() );
  const auto primary = report.primaryBlocking();
  REQUIRE( primary.has_value() );
  CHECK( primary->verdict == GridCompatVerdict::OriginMisalignment );
  CHECK( primary->code == QStringLiteral( "grid.origin_misalignment" ) );
  CHECK( primary->message.contains( QStringLiteral( "sub-pixel" ) ) );
}

TEST_CASE( "A whole-pixel shift and a smaller extent are extent mismatches", "[grid_compat]" )
{
  SECTION( "Whole-pixel shift" )
  {
    // Origin shifted exactly one 30 m pixel east: same lattice, shifted extent.
    const RasterGrid b = makeGrid( 100, 100, 500030.0, 4500000.0 );
    const GridCompatReport report = sicnu::data::compareGrids( makeGrid(), b );
    CHECK_FALSE( report.compatible() );
    REQUIRE( firstIssue( report, GridCompatVerdict::ExtentMismatch ) != nullptr );
  }
  SECTION( "Different dimensions, same origin" )
  {
    const RasterGrid b = makeGrid( 50, 100 );
    const GridCompatReport report = sicnu::data::compareGrids( makeGrid(), b );
    CHECK_FALSE( report.compatible() );
    const auto primary = report.primaryBlocking();
    REQUIRE( primary.has_value() );
    CHECK( primary->verdict == GridCompatVerdict::ExtentMismatch );
    CHECK( primary->message.contains( QStringLiteral( "clip" ) ) );
  }
  SECTION( "Disjoint extents" )
  {
    const RasterGrid b = makeGrid( 100, 100, 5'000'000.0, 4'500'000.0 );
    const GridCompatReport report = sicnu::data::compareGrids( makeGrid(), b );
    CHECK_FALSE( report.compatible() );
    const auto primary = report.primaryBlocking();
    REQUIRE( primary.has_value() );
    CHECK( primary->verdict == GridCompatVerdict::ExtentMismatch );
    CHECK( primary->message.contains( QStringLiteral( "do not overlap" ) ) );
  }
}

TEST_CASE( "Differing NoData values are a non-blocking warning", "[grid_compat]" )
{
  RasterGrid b = makeGrid();
  b.bandNoData = { std::optional<double>( 0.0 ) };

  const GridCompatReport report = sicnu::data::compareGrids( makeGrid(), b );
  CHECK( report.compatible() ); // no blocking issue
  CHECK_FALSE( report.aligned() );
  const GridCompatIssue *issue = firstIssue( report, GridCompatVerdict::NoDataMismatch );
  REQUIRE( issue != nullptr );
  CHECK_FALSE( issue->blocking );
  CHECK( issue->code == QStringLiteral( "grid.nodata_mismatch" ) );
  CHECK( issue->message.contains( QStringLiteral( "NoData" ) ) );
}

TEST_CASE( "compareStructures works over RasterStructure values", "[grid_compat]" )
{
  RasterStructure a;
  a.crsWkt = QStringLiteral( "EPSG:32648" );
  a.hasGeoTransform = true;
  a.geoTransform = { 500000, 30, 0, 4500000, 0, -30 };
  a.width = 100;
  a.height = 100;
  RasterBandStructure band;
  band.number = 1;
  band.noDataValue = -9999.0;
  a.bands.append( band );

  RasterStructure b = a;
  b.geoTransform[1] = 60.0;
  b.geoTransform[5] = -60.0;
  b.width = 50;
  b.height = 50;

  const GridCompatReport report = sicnu::data::compareStructures( a, b );
  CHECK_FALSE( report.compatible() );
  REQUIRE( firstIssue( report, GridCompatVerdict::PixelSizeMismatch ) != nullptr );

  // Identical structures are fully compatible.
  const GridCompatReport same = sicnu::data::compareStructures( a, a );
  CHECK( same.aligned() );
}
